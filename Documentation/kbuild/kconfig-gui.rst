.. SPDX-License-Identifier: GPL-2.0

=====================
Kconfig GUI Interface
=====================

This document describes the graphical user interface for kernel configuration.

wconf - Tauri-based Configuration Tool
=====================================

Overview
--------

``wconf`` is a modern, web-based interface for configuring the Linux kernel,
built using the Tauri framework. It provides a graphical alternative to the
traditional text-based kernel configuration tools with a web-based user
interface that runs as a desktop application.

Features
--------

- Modern web-based user interface
- Tree view of kernel configuration options
- Search functionality for easy navigation
- Real-time dependency checking
- Help text display for each configuration option
- Cross-platform support (Linux, Windows, macOS)

Usage
-----

To use the Tauri-based configuration interface, run::

    make wconfig

This will launch the graphical configuration tool, allowing you to view and
modify kernel configuration options.

Requirements
------------

- Rust 1.70 or later
- Node.js 18 or later
- Tauri CLI (automatically downloaded during build)

Implementation Details
----------------------

The ``wconf`` tool consists of:

1. A Rust backend that parses Kconfig files and manages the configuration state
2. A web-based frontend using HTML, CSS, and JavaScript
3. Tauri framework for creating the desktop application

The tool integrates with the existing Kbuild system through the ``wconfig``
target in ``scripts/kconfig/Makefile``.

Development
-----------

The source code for ``wconf`` is located in ``scripts/kconfig/wconf/`` and
includes:

- ``Cargo.toml``: Rust package manifest
- ``src/main.rs``: Main Rust application
- ``src/kconfig.rs``: Kconfig parser implementation
- ``index.html``: Main HTML interface
- ``tauri.conf.json``: Tauri configuration
- ``package.json``: Frontend dependencies

To build manually during development::

    cd scripts/kconfig/wconf
    cargo build

The application follows the same build and distribution model as other kernel
configuration tools.
