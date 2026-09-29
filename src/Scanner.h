// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 David Eichendorf and FileClump contributors
// Fast parallel filesystem scanner. Qt-free on purpose: it runs on worker
// threads and only produces a compact, arena-allocated tree.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum NodeType : uint8_t {
    NT_File = 0,
    NT_Dir = 1,
    NT_Group = 2, // synthetic "[N Files in <dir>]" node holding a directory's files
};

enum NodeFlag : uint8_t {
    NF_Error = 1,    // could not stat / open
    NF_OtherFs = 2,  // mount point not descended into
    NF_Deleted = 4,  // removed by the user after the scan
    NF_Symlink = 8,
    NF_Hardlink = 16, // additional link of an already counted inode
    NF_Virtual = 32,  // /proc, /sys and similar pseudo filesystems
};

// 64 bytes. Children of a node are stored contiguously, which makes
// "row of node in parent" a pointer subtraction.
struct Node {
    const char* name = nullptr; // nullptr for group nodes
    Node* parent = nullptr;
    Node* kids = nullptr;
    uint64_t size = 0;  // apparent size (aggregated for containers)
    uint64_t alloc = 0; // allocated on disk
    int64_t mtime = 0;  // newest mtime in subtree for containers
    uint32_t nkids = 0;
    uint32_t files = 0;   // files in subtree (0 for files)
    uint32_t folders = 0; // folders in subtree
    uint16_t ext = 0;     // extension id, files only
    uint8_t type = NT_File;
    uint8_t flags = 0;

    bool isContainer() const { return type != NT_File; }
    int row() const { return parent ? int(this - parent->kids) : 0; }
};
static_assert(sizeof(Node) == 64, "Node should stay compact");

// Bump allocator; one per scanning thread, so no locking.
class Arena {
public:
    void* alloc(size_t n, size_t align);
    Node* nodes(size_t n);
    const char* str(const char* s, size_t len);
    size_t bytes() const { return m_total; }

private:
    std::vector<std::unique_ptr<char[]>> m_blocks;
    char* m_cur = nullptr;
    size_t m_left = 0;
    size_t m_total = 0;
};

struct ExtInfo {
    std::string name; // lowercase, without dot; "" = no extension
    uint64_t size = 0;
    uint64_t alloc = 0;
    uint32_t files = 0;
    uint32_t color = 0x808080;
    uint32_t rank = 0;
};

struct ScanResult {
    std::vector<std::unique_ptr<Arena>> arenas;
    Node* root = nullptr;
    std::string rootPath;
    std::vector<ExtInfo> exts; // id 0 = no extension
    uint64_t errors = 0;
    double seconds = 0;
    bool cancelled = false;

    std::string fullPath(const Node* n) const;
    size_t memoryBytes() const;
    static bool isDeleted(const Node* n);
    // Mark as deleted and subtract its totals from all ancestors / extension stats.
    void remove(Node* n);

private:
    void subtractExts(const Node* n);
};

class Scanner {
public:
    struct Options {
        bool oneFileSystem = true;
        bool dedupHardlinks = true;
        unsigned threads = 0; // 0 = auto
    };

    // Live progress, readable from any thread.
    std::atomic<uint64_t> files{0};
    std::atomic<uint64_t> dirs{0};
    std::atomic<uint64_t> bytes{0};
    std::atomic<bool> cancel{false};

    // Blocking. Returns a partial result when cancelled.
    std::unique_ptr<ScanResult> run(const std::string& path, const Options& opt);
};
