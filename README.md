# DVF (Vamped DNF Dandified YUM)

### ⚠️ Under Development...
This repository is a **Work in Progress (WIP)**.
* **Current Stage:** Alpha / Prototyping
* **Stability:** Unstable. Expect breaking updates.
* **Contributions:** Not accepting external pull requests quite yet, but feel free to open an issue for discussion!

DVF is a high-performance and vamped up reimplementation of `dnf` (Dandified YUM). It features a hybrid architecture combining a lightweight C core for local operations with a powerful C++ FFI layer for advanced repository management and Finite State Machine (FSM) based mirror selection.

## Features

- **Blazing Fast Autocomplete**: Uses a memory-mapped binary index to provide near-instant completion for millions of package names (both installed and remote).
- **Interleaved Commands**: Execute multiple actions in a single command line (e.g., `dvf update install htop`).
- **Hybrid Architecture**: Core C logic ensures minimal footprint, while C++17 handles complex networking and metadata parsing.
- **FSM Mirror Selection**: Advanced Finite State Machine that dynamically ranks and selects the most reliable mirrors based on real-time metadata.
- **Robust Metadata Parsing**: High-performance streaming XML parser for handling massive repository metadata files (`primary.xml.zst/gz`).
- **RPMDB Integration**: Direct, zero-dependency reading of the system's `rpmdb.sqlite` for lightning-fast local package lookups.

## Usage & Commands

DVF supports several commands, which can be interleaved:

- `update`: Synchronize repository metadata from remote mirrors.
- `install <pkg>`: Resolve and install a package (FFI mode).
- `remove <pkg>`: Remove an installed package.
- `search <term>`: Search for packages in name and summary.
- `info <pkg>`: Display detailed RPM header information.
- `check-update`: Check for available package updates across all enabled repositories.
- `sync`: Rebuild the local autocomplete index by merging `rpmdb` and repository metadata.

**Example:**
```bash
dvf update sync install conky
```

## Directory Structure

- `dvf/`: Source code and build system.
  - `dvf-main.c`: CLI entry point and command router.
  - `dvf-repo.cpp`: C++ FFI layer for repository and FSM logic.
  - `dvf-completion.c`: Autocomplete indexer and search logic.
  - `dvf-sqlite.c`: Direct SQLite parser for `rpmdb`.
  - `dvf-rpm.c`: Low-level RPM header parser.
  - `dvf-config.c`: System-wide configuration management.

## Dependencies
- `libcurl-devel`
- `libzstd` (for metadata decompression)
- `gcc` / `g++` (C++17 support required)

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

After installation, run a sync to initialize your autocomplete index:
```bash
dvf sync
```

## License

This project is licensed under the GPL v3 - see the `LICENSE` file for details.
