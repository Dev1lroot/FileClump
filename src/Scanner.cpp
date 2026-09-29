// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 David Eichendorf and FileClump contributors
#include "Scanner.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <numeric>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#ifndef STATX_MNT_ID
#define STATX_MNT_ID 0x00001000U
#endif

namespace {

constexpr size_t kBlock = 1 << 20;

// Rank-ordered palette, as in classic treemap tools: the biggest extension
// gets orange, the next yellow, and so on. Everything else is gray.
constexpr uint32_t kPalette[] = {
    0xE67800, 0xC8C800, 0x14B414, 0x9030E6, 0xE62896, 0x1E6EFF,
    0x50A0FF, 0xDC28DC, 0xE61414, 0x8C5AFF, 0x00C8AA, 0xAA6414,
    0x8CDC28, 0x00BEE6, 0xFF6E6E, 0x969600, 0xB4B4FF, 0xFFB450,
};
constexpr uint32_t kGray = 0x7A7A7A;

struct Entry {
    const char* name;
    uint64_t size;
    uint64_t alloc;
    int64_t mtime;
    uint64_t dev;
    uint64_t mnt;
    bool dir;
    uint8_t flags;
};

struct Task {
    Node* node;
    std::string path;
    uint64_t dev;
    uint64_t mnt;
};

struct PairHash {
    size_t operator()(const std::pair<uint64_t, uint64_t>& p) const noexcept
    {
        return std::hash<uint64_t>()(p.first * 0x9E3779B97F4A7C15ull ^ p.second);
    }
};

class InodeSet {
public:
    bool insert(uint64_t dev, uint64_t ino)
    {
        Shard& sh = m_shards[(ino ^ dev) & 63];
        std::lock_guard lk(sh.m);
        return sh.s.emplace(dev, ino).second;
    }

private:
    struct Shard {
        std::mutex m;
        std::unordered_set<std::pair<uint64_t, uint64_t>, PairHash> s;
    };
    Shard m_shards[64];
};

bool isVirtualFs(const std::string& path)
{
    struct statfs sf;
    if (statfs(path.c_str(), &sf) != 0)
        return false;
    switch (static_cast<unsigned long>(sf.f_type)) {
    case 0x9fa0:     // proc
    case 0x62656572: // sysfs
    case 0x1cd1:     // devpts
    case 0x1373:     // devfs
    case 0x64626720: // debugfs
    case 0x74726163: // tracefs
    case 0x27e0eb:   // cgroup
    case 0x63677270: // cgroup2
    case 0x73636673: // securityfs
    case 0x6165676c: // pstore
    case 0xcafe4a11: // bpf
    case 0x62656570: // configfs
    case 0x65735543: // fusectl
    case 0xde5e81e4: // efivarfs
    case 0x42494e4d: // binfmt_misc
    case 0x19800202: // mqueue
    case 0x958458f6: // hugetlbfs
    case 0x6e736673: // nsfs
    case 0x0187:     // autofs
        return true;
    }
    return false;
}

std::string joinPath(const std::string& dir, const char* name)
{
    std::string p;
    p.reserve(dir.size() + strlen(name) + 1);
    p = dir;
    if (p.empty() || p.back() != '/')
        p += '/';
    p += name;
    return p;
}

constexpr unsigned kStatxMask = STATX_TYPE | STATX_MODE | STATX_SIZE | STATX_BLOCKS | STATX_MTIME
    | STATX_INO | STATX_NLINK | STATX_MNT_ID;

struct Job {
    Scanner& sc;
    const Scanner::Options& opt;

    std::mutex m;
    std::condition_variable cv;
    std::vector<Task> stack;
    unsigned active = 0;
    InodeSet inodes;
    std::atomic<uint64_t> errors{0};

    Job(Scanner& s, const Scanner::Options& o) : sc(s), opt(o) {}

    void worker(Arena& A)
    {
        std::vector<Entry> ents;
        std::vector<Task> fresh;
        for (;;) {
            Task t;
            {
                std::unique_lock lk(m);
                cv.wait(lk, [&] { return !stack.empty() || active == 0 || sc.cancel.load(std::memory_order_relaxed); });
                if (stack.empty() || sc.cancel.load(std::memory_order_relaxed)) {
                    lk.unlock();
                    cv.notify_all();
                    return;
                }
                t = std::move(stack.back());
                stack.pop_back();
                ++active;
            }
            fresh.clear();
            scanDir(A, t, ents, fresh);
            size_t nf = fresh.size();
            bool finished;
            {
                std::lock_guard lk(m);
                for (auto it = fresh.rbegin(); it != fresh.rend(); ++it)
                    stack.push_back(std::move(*it));
                --active;
                finished = active == 0 && stack.empty();
            }
            if (finished || nf > 1)
                cv.notify_all();
            else if (nf == 1)
                cv.notify_one();
        }
    }

    void scanDir(Arena& A, Task& t, std::vector<Entry>& ents, std::vector<Task>& fresh)
    {
        Node* dir = t.node;
        int fd = ::open(t.path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) {
            dir->flags |= NF_Error;
            errors.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        DIR* d = fdopendir(fd);
        if (!d) {
            ::close(fd);
            dir->flags |= NF_Error;
            errors.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        ents.clear();
        uint32_t ndirs = 0, nfiles = 0;
        uint64_t fileBytes = 0;
        while (dirent* de = readdir(d)) {
            const char* nm = de->d_name;
            if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0)))
                continue;
            Entry e {};
            e.name = A.str(nm, strlen(nm));
            struct statx sx;
            if (statx(fd, nm, AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT | AT_STATX_DONT_SYNC, kStatxMask, &sx) != 0) {
                e.flags = NF_Error;
                e.dir = de->d_type == DT_DIR;
                errors.fetch_add(1, std::memory_order_relaxed);
            } else {
                e.mtime = sx.stx_mtime.tv_sec;
                e.dev = makedev(sx.stx_dev_major, sx.stx_dev_minor);
                e.mnt = (sx.stx_mask & STATX_MNT_ID) ? sx.stx_mnt_id : 0;
                if (S_ISDIR(sx.stx_mode)) {
                    e.dir = true;
                    bool crossed = (e.mnt && t.mnt) ? e.mnt != t.mnt : e.dev != t.dev;
                    if (crossed) {
                        if (opt.oneFileSystem)
                            e.flags |= NF_OtherFs;
                        else if (isVirtualFs(joinPath(t.path, nm)))
                            e.flags |= NF_OtherFs | NF_Virtual;
                    }
                } else {
                    if (S_ISREG(sx.stx_mode) || S_ISLNK(sx.stx_mode)) {
                        e.size = sx.stx_size;
                        e.alloc = uint64_t(sx.stx_blocks) * 512u;
                    }
                    if (S_ISLNK(sx.stx_mode))
                        e.flags |= NF_Symlink;
                    if (opt.dedupHardlinks && S_ISREG(sx.stx_mode) && sx.stx_nlink > 1
                        && !inodes.insert(e.dev, sx.stx_ino)) {
                        e.size = e.alloc = 0;
                        e.flags |= NF_Hardlink;
                    }
                    fileBytes += e.size;
                }
            }
            e.dir ? ++ndirs : ++nfiles;
            ents.push_back(e);
        }
        closedir(d);

        sc.dirs.fetch_add(1, std::memory_order_relaxed);
        sc.files.fetch_add(nfiles, std::memory_order_relaxed);
        sc.bytes.fetch_add(fileBytes, std::memory_order_relaxed);

        uint32_t nk = ndirs + (nfiles ? 1 : 0);
        if (nk == 0)
            return;
        Node* kids = A.nodes(nk);
        Node* files = nfiles ? A.nodes(nfiles) : nullptr;
        Node* group = nfiles ? &kids[ndirs] : nullptr;
        if (group) {
            group->type = NT_Group;
            group->parent = dir;
            group->kids = files;
            group->nkids = nfiles;
        }
        uint32_t di = 0, fi = 0;
        for (const Entry& e : ents) {
            if (e.dir) {
                Node& k = kids[di++];
                k.name = e.name;
                k.parent = dir;
                k.type = NT_Dir;
                k.mtime = e.mtime;
                k.flags = e.flags;
                if (!(e.flags & (NF_Error | NF_OtherFs)))
                    fresh.push_back({&k, joinPath(t.path, e.name), e.dev, e.mnt});
            } else {
                Node& k = files[fi++];
                k.name = e.name;
                k.parent = group;
                k.type = NT_File;
                k.size = e.size;
                k.alloc = e.alloc;
                k.mtime = e.mtime;
                k.flags = e.flags;
            }
        }
        dir->kids = kids;
        dir->nkids = nk;
    }
};

// Aggregates totals bottom-up, sorts children by size, fixes parent pointers
// (sorting moves nodes) and interns file extensions.
struct Finalizer {
    ScanResult& res;
    std::unordered_map<std::string, uint16_t> map;

    explicit Finalizer(ScanResult& r) : res(r)
    {
        res.exts.clear();
        res.exts.emplace_back(); // id 0: no extension
    }

    uint16_t extId(const char* name)
    {
        const char* dot = strrchr(name, '.');
        if (!dot || dot == name || dot[1] == 0)
            return 0;
        size_t len = strlen(dot + 1);
        // Versioned names like "libfoo.so.1.2.3": skip numeric parts to reach "so".
        auto numeric = [](const char* s, size_t n) {
            for (size_t i = 0; i < n; ++i)
                if (s[i] < '0' || s[i] > '9')
                    return false;
            return true;
        };
        if (numeric(dot + 1, len)) {
            const char* end = dot;
            const char* p = dot;
            while (p > name) {
                const char* q = p - 1;
                while (q > name && *q != '.')
                    --q;
                if (q == name || *q != '.')
                    break;
                if (!numeric(q + 1, size_t(end - q - 1))) {
                    dot = q;
                    len = size_t(end - q - 1);
                    break;
                }
                end = p = q;
            }
        }
        if (len > 16)
            return 0;
        char buf[17];
        for (size_t i = 0; i < len; ++i) {
            char c = dot[1 + i];
            if (c == ' ')
                return 0;
            buf[i] = (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        }
        std::string key(buf, len);
        auto it = map.find(key);
        if (it != map.end())
            return it->second;
        if (res.exts.size() >= 0xFFFF)
            return 0;
        uint16_t id = uint16_t(res.exts.size());
        res.exts.emplace_back();
        res.exts.back().name = key;
        map.emplace(std::move(key), id);
        return id;
    }

    void run(Node* n)
    {
        uint64_t s = 0, a = 0;
        uint32_t f = 0, d = 0;
        int64_t mt = n->mtime;
        for (uint32_t i = 0; i < n->nkids; ++i) {
            Node& k = n->kids[i];
            if (k.type != NT_File) {
                run(&k);
                f += k.files;
                d += k.folders + (k.type == NT_Dir ? 1 : 0);
            } else {
                ++f;
                k.ext = extId(k.name);
                ExtInfo& e = res.exts[k.ext];
                e.size += k.size;
                e.alloc += k.alloc;
                ++e.files;
            }
            s += k.size;
            a += k.alloc;
            mt = std::max(mt, k.mtime);
        }
        n->size = s;
        n->alloc = a;
        n->files = f;
        n->folders = d;
        n->mtime = mt;
        if (n->nkids > 1) {
            std::sort(n->kids, n->kids + n->nkids, [](const Node& x, const Node& y) {
                if (x.size != y.size)
                    return x.size > y.size;
                return strcmp(x.name ? x.name : "", y.name ? y.name : "") < 0;
            });
        }
        for (uint32_t i = 0; i < n->nkids; ++i) {
            Node& k = n->kids[i];
            k.parent = n;
            for (uint32_t j = 0; j < k.nkids; ++j)
                k.kids[j].parent = &k;
        }
    }

    void assignColors()
    {
        std::vector<uint32_t> order(res.exts.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) { return res.exts[x].size > res.exts[y].size; });
        constexpr size_t np = sizeof(kPalette) / sizeof(kPalette[0]);
        for (size_t r = 0; r < order.size(); ++r) {
            ExtInfo& e = res.exts[order[r]];
            e.rank = uint32_t(r);
            e.color = (r < np && e.size > 0) ? kPalette[r] : kGray;
        }
    }
};

} // namespace

void* Arena::alloc(size_t n, size_t align)
{
    auto padFor = [align](char* p) { return (align - (reinterpret_cast<uintptr_t>(p) & (align - 1))) & (align - 1); };
    if (n > kBlock / 4) { // big arrays get a dedicated block; keep the current one
        m_blocks.emplace_back(new char[n + align]);
        m_total += n + align;
        char* p = m_blocks.back().get();
        return p + padFor(p);
    }
    size_t pad = m_cur ? padFor(m_cur) : 0;
    if (!m_cur || pad + n > m_left) {
        m_blocks.emplace_back(new char[kBlock]);
        m_total += kBlock;
        m_cur = m_blocks.back().get();
        m_left = kBlock;
        pad = padFor(m_cur);
    }
    char* p = m_cur + pad;
    m_cur = p + n;
    m_left -= pad + n;
    return p;
}

Node* Arena::nodes(size_t n)
{
    Node* p = static_cast<Node*>(alloc(n * sizeof(Node), alignof(Node)));
    for (size_t i = 0; i < n; ++i)
        new (p + i) Node();
    return p;
}

const char* Arena::str(const char* s, size_t len)
{
    char* p = static_cast<char*>(alloc(len + 1, 1));
    memcpy(p, s, len);
    p[len] = 0;
    return p;
}

std::string ScanResult::fullPath(const Node* n) const
{
    std::vector<const Node*> chain;
    for (const Node* p = n; p; p = p->parent)
        if (p->type != NT_Group)
            chain.push_back(p);
    if (chain.empty())
        return rootPath;
    std::string s = rootPath;
    for (size_t i = chain.size() - 1; i-- > 0;) {
        if (s.empty() || s.back() != '/')
            s += '/';
        s += chain[i]->name;
    }
    return s;
}

size_t ScanResult::memoryBytes() const
{
    size_t t = 0;
    for (auto& a : arenas)
        t += a->bytes();
    return t;
}

bool ScanResult::isDeleted(const Node* n)
{
    for (; n; n = n->parent)
        if (n->flags & NF_Deleted)
            return true;
    return false;
}

void ScanResult::subtractExts(const Node* n)
{
    if (n->type == NT_File) {
        ExtInfo& e = exts[n->ext];
        e.size -= n->size;
        e.alloc -= n->alloc;
        --e.files;
        return;
    }
    for (uint32_t i = 0; i < n->nkids; ++i)
        if (!(n->kids[i].flags & NF_Deleted))
            subtractExts(&n->kids[i]);
}

void ScanResult::remove(Node* n)
{
    if (!n || n == root || isDeleted(n))
        return;
    subtractExts(n);
    uint64_t s = n->size, a = n->alloc;
    uint32_t f = n->type == NT_File ? 1 : n->files;
    uint32_t d = n->folders + (n->type == NT_Dir ? 1 : 0);
    for (Node* p = n->parent; p; p = p->parent) {
        p->size -= s;
        p->alloc -= a;
        p->files -= f;
        p->folders -= d;
    }
    n->flags |= NF_Deleted;
}

std::unique_ptr<ScanResult> Scanner::run(const std::string& path0, const Options& opt)
{
    auto t0 = std::chrono::steady_clock::now();
    auto res = std::make_unique<ScanResult>();

    std::string path = path0;
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();
    res->rootPath = path;

    unsigned nt = opt.threads ? opt.threads : std::clamp(std::thread::hardware_concurrency() * 2, 4u, 32u);
    for (unsigned i = 0; i <= nt; ++i)
        res->arenas.push_back(std::make_unique<Arena>());

    Arena& A0 = *res->arenas[nt];
    Node* root = A0.nodes(1);
    root->name = A0.str(path.c_str(), path.size());
    root->type = NT_Dir;
    res->root = root;

    struct statx sx;
    if (statx(AT_FDCWD, path.c_str(), AT_STATX_DONT_SYNC, kStatxMask, &sx) != 0 || !S_ISDIR(sx.stx_mode)) {
        root->flags |= NF_Error;
        res->errors = 1;
        return res;
    }
    root->mtime = sx.stx_mtime.tv_sec;

    Job job(*this, opt);
    job.stack.push_back({root, path, makedev(sx.stx_dev_major, sx.stx_dev_minor),
        (sx.stx_mask & STATX_MNT_ID) ? sx.stx_mnt_id : 0});

    std::vector<std::thread> threads;
    threads.reserve(nt);
    for (unsigned i = 0; i < nt; ++i)
        threads.emplace_back([&job, &res, i] { job.worker(*res->arenas[i]); });
    for (auto& t : threads)
        t.join();

    Finalizer fin(*res);
    fin.run(root);
    fin.assignColors();

    res->errors = job.errors.load();
    res->cancelled = cancel.load();
    res->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}
