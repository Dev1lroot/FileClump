# Contributing to FileClump

Bug fixes, features and ideas are welcome.

## Getting started

```sh
sudo apt install qt6-base-dev cmake g++
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/fileclump /some/folder
```

## Guidelines

- Keep it native and light. Use C++20 and Qt 6 Widgets only, with no web runtimes and no heavy new dependencies.
- Keep the scanner (`src/Scanner.*`) Qt-free. It runs on worker threads.
- Match the style of the surrounding code. New source files start with:
  ```cpp
  // SPDX-License-Identifier: GPL-2.0-only
  // Copyright (C) 2026 David Eichendorf and FileClump contributors
  ```
- Don't copy code, icons or other assets from other disk analyzers (WizTree, WinDirStat, SequoiaView, …).
  FileClump is written from scratch.
- Build with no compiler warnings (`-Wall -Wextra`) before you open a pull request.
- Add yourself to the **Contributors** list in [AUTHORS.md](AUTHORS.md).

## License

FileClump is licensed under the GNU General Public License version 2 only (GPL-2.0-only).
By contributing, you agree that your contributions are licensed under the same terms.
