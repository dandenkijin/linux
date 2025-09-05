# Development Setup for wconf

This document describes how to set up and run the wconf development environment.

## Prerequisites

- Node.js (v14 or later)
- npm (comes with Node.js)
- Rust toolchain (for Tauri)
- System dependencies for Tauri development

## Development Workflow

### Running in Development Mode

To run the configuration interface with development mode enabled:

```bash
make wconfig WCONFIG_DEBUG=1
```

This will:
- Enable additional debug logging
- Include development tools
- Show debug menu options

### Advanced Development

For more control over the development environment, you can use the development script:

```bash
cd linux/scripts/kconfig
./run-wconf-dev.sh
```

2. The development server will:
   - Install any missing dependencies
   - Build wconf with development mode enabled
   - Start the Tauri development server

3. Access the development interface at the URL provided in the terminal output.

## Building for Production

To build the production version of wconf:

```bash
make wconfig
```

Or for a one-time build without running the interface:

```bash
make -C scripts/kconfig wconf
```

```bash
make -C linux/scripts/kconfig clean
make -C linux/scripts/kconfig wconfig
```

## Development Mode Features

When running with `WCONFIG_DEBUG=1`, the following features are enabled:
- Additional debug logging
- Development tools and hot-reloading
- Debug menu options

## Troubleshooting

- If you encounter build issues, try running `npm install` in the `scripts/kconfig/wconf-app` directory
- Make sure all system dependencies for Tauri are installed
- Check the browser's developer console for any frontend errors
