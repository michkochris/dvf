# DVF (Vamped Up DNF Dandified YUM)

### ⚠️ Under Development...
This repository is a **Work in Progress (WIP)**.
* **Current Stage:** Alpha / Prototyping
* **Stability:** Unstable. Expect breaking updates.
* **Contributions:** Not accepting external pull requests quite yet, but feel free to open an issue for discussion!

DVF is a high-performance, vamped-up reimplementation of `dnf` (Dandified YUM). It features a hybrid architecture combining a lightweight C core for local operations with a powerful C++ FFI layer for advanced repository management and Finite State Machine (FSM) based mirror selection.

## Key Features

- **🚀 Blazing Fast Autocomplete**: Uses a memory-mapped binary index to provide near-instant completion for millions of package names, including both installed and remote repository entries.
- **⚡ High-Speed Metadata Caching**: Implements a custom binary format (`metadata.bin`) that caches repository data after synchronization. This avoids expensive XML parsing during installation and allows for $O(\log N)$ dependency resolution.
- **📦 Advanced Dependency Resolver**: Supports recursive, indefinite dependency trees. Handles complex Fedora-specific requirements including:
  - **Rich Dependencies**: Boolean expressions like `(pkg1 if pkg2)`.
  - **Virtual Capabilities**: Resolves dependencies by provide name (e.g., `libmagic.so.1`).
  - **File Providers**: Automatically identifies packages providing specific files (e.g., `/usr/bin/sh`).
- **🛡️ Environment Isolation**: Robust support for `install_root`. Successfully bootstraps entire systems into isolated directories (e.g., `/mnt/lfs`) by strictly separating host and guest metadata/filesystems.
- **🎨 Professional UX**: 
  - **DNF-Style Output**: Formatted transaction summary tables with Package, Architecture, Version, and Repository.
  - **Progress Indicators**: Real-time visual progress bars for metadata synchronization and package downloads.
  - **ANSI Colors**: Clear status signaling with standard color-coding (`[DONE]`, `[FAILED]`).
- **🔗 Hybrid Architecture**: Core C logic ensures a minimal footprint for local queries, while C++17 handles complex networking, FSM mirror selection, and metadata indexing.
- **📊 RPMDB Integration**: Direct, zero-dependency reading of the system's `rpmdb.sqlite` for lightning-fast local package lookups.

## Usage & Commands

DVF supports several commands, which can be interleaved:

- `update`: Synchronize repository metadata from remote mirrors and rebuild the high-speed binary cache.
- `install <pkg|capability|file>`: Resolve and install a package, a virtual capability, or a specific file path.
- `upgrade`: Upgrade all installed packages to the latest available versions.
- `remove <pkg>`: Remove an installed package.
- `search <term>`: Search for packages in name and summary.
- `info <pkg>`: Display detailed RPM header information.
- `check-update`: Check for available package updates across all enabled repositories.
- `sync`: Rebuild the local autocomplete index by merging `rpmdb` and repository metadata.

**Example:**
```bash
dvf update install xfce4-terminal
```

## Directory Structure

- `dvf/`: Source code and build system.
  - `dvf-main.c`: CLI entry point and command router.
  - `dvf-repo.cpp`: C++ FFI layer for repository management, FSM logic, and binary caching.
  - `dvf-metadata.h`: Definition of the high-speed binary metadata cache format.
  - `dvf-completion.c`: Autocomplete indexer and search logic.
  - `dvf-sqlite.c`: Direct SQLite parser for `rpmdb`.
  - `dvf-rpm.c`: Low-level RPM header parser and unpacker.
  - `dvf-config.c`: System-wide configuration management.

## Dependencies
- `libcurl-devel`
- `libzstd` (for metadata decompression)
- `openssl-devel`
- `gcc` / `g++` (C++17 support required)
- `cpio` (for package extraction)


## Building and Installation

To build the full version with C++ FFI support:
```bash
cd dvf
make all
```

To install to your system:
```bash
cd dvf
sudo make install
```

After installation, synchronize your metadata and initialize the autocomplete index:
```bash
sudo dvf update
```

## License

This project is licensed under the GPL v3 - see the `LICENSE` file for details.
