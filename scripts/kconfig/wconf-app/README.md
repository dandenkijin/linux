# wconf - Tauri-based Kernel Configuration Tool


## Features

- Modern web-based user interface
- Hierarchical tree view of kernel configuration options
- Expandable/collapsible menu structure
- Search functionality for easy navigation
- Real-time dependency checking
- Help text display for each configuration option
- Cross-platform support (Linux, Windows, macOS)

This is a modern, web-based interface for configuring the Linux kernel, built using the Tauri framework. It provides a graphical alternative to the traditional text-based kernel configuration tools with a hierarchical tree view.

## Overview


## Requirements

- Rust 1.70 or later
- Node.js 18 or later
- Tauri CLI (automatically downloaded during build)

## Features

- Modern web-based user interface
- Tree view of kernel configuration options
- Search functionality for easy navigation
- Real-time dependency checking
- Help text display for each configuration option

## Requirements


## Building

The tool is automatically built when invoking `make wconfig` from the kernel source root directory.

To build manually:
```bash
cd scripts/kconfig/wconf-app
npm install
npm run build
cargo build --release --features custom-protocol
```

## Building

The tool is automatically built when invoking `make wconfig` from the kernel source root directory.


## Usage

From the kernel source root directory:
```bash
make wconfig
```

This will launch the Tauri-based configuration interface, allowing you to view and modify kernel configuration options.

## Development

- Frontend: HTML/CSS/JavaScript with tree view implementation
- Backend: Rust with AST-based Kconfig parser
- Communication: Tauri invoke API
- Build system: Vite + Cargo

## Documentation

- Implementation details: IMPLEMENTATION_SUMMARY.md
- Git workflow: GIT_WORKFLOW.md
- User documentation: Documentation/kbuild/kconfig-gui.rst

## Usage

From the kernel source root directory:
```bash
make wconfig
```

This will launch the Tauri-based configuration interface, allowing you to view and modify kernel configuration options.