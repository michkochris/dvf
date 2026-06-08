# DVF (Vamped DNF)

DVF is a high-performance successor to `dnf` (Dandified YUM), designed with a modular architecture that combines a lightweight C core with a powerful C++ FFI layer for advanced repository management and Finite State Machine (FSM) based mirror selection.

## Features

- **Interleaved Commands**: Run multiple commands in a single invocation (e.g., `dvf update install <pkg>`).
- **Modular Architecture**: core C logic for local operations, C++ FFI for networking and complex state machines.
- **FSM-based Mirror Selection**: Dynamically ranks and selects the best mirrors based on latency and reliability.
- **Runepkg-style Configuration**: Cascading configuration system with system-wide and user-level path overrides.

## Directory Structure

- `README.md`: Project overview.
- `LICENSE`: Licensing information.
- `dvf/`: Contains the source code and build system.
  - `Makefile`: Build instructions.
  - `dvf-main.c`: Core CLI entry point.
  - `dvf-repo.cpp/h`: C++ FFI layer for repository handling.
  - `dvf-config.c/h`: Configuration management.
  - `dvf-util.c/h`: Shared utilities.

## Building and Installation

To build the core version (C-only):
```bash
cd dvf
make dvf
```

To build the full version with C++ FFI:
```bash
cd dvf
make all
```

To install to your system:
```bash
cd dvf
sudo make install
```

## License

This project is licensed under the GPL v3 - see the `LICENSE` file for details.
