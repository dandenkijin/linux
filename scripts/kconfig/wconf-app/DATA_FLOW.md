# Data and Process Flow for `wconf`

This document outlines the complete information and process flow for the `make wconfig` kernel configuration tool, from user invocation to UI rendering. Understanding this flow is critical for debugging issues like application hangs or data not appearing in the frontend.

## 1. Invocation

The entire process begins when the user runs the `make` command from the kernel source root.

- **User Command**: `make wconfig`
- **Action**: The top-level `Makefile` finds the `wconfig` target.
- **Delegation**: The request is delegated to the Kconfig Makefile via the rule: `$(Q)$(MAKE) $(build)=scripts/kconfig $@`. This effectively runs `make -C scripts/kconfig wconfig`.

## 2. C Wrapper Compilation and Execution

The `scripts/kconfig/Makefile` is responsible for building and running the C wrapper, which acts as the orchestrator for the frontend and backend.

- **Compilation**: `scripts/kconfig/wconf.c` is compiled into a host executable named `scripts/kconfig/wconf`.
- **Execution**: The `wconfig:` rule in the Makefile executes the compiled wrapper: `scripts/kconfig/wconf Kconfig`.
- **Argument Passing**: The name of the root Kconfig file (e.g., `Kconfig`) is passed as a command-line argument to the `wconf` executable.

## 3. Process Orchestration (The C Wrapper)

The `wconf` C program is responsible for launching and managing the two key components of the application: the frontend development server and the Rust backend. It uses a "fork-wait-kill" pattern to ensure clean startup and shutdown.

1.  **Fork Frontend**: The wrapper forks a child process.
    -   **Action**: This child process changes its directory to `scripts/kconfig/wconf-app/`.
    -   **Execution**: It then executes `npm run dev` to start the Vite frontend development server. This server watches for file changes and serves the HTML, CSS, and JavaScript.
2.  **Fork Backend**: The wrapper forks a second child process.
    -   **Action**: This child process also changes its directory to `scripts/kconfig/wconf-app/`.
    -   **Output Redirection**: It redirects its `stdout` and `stderr` to `wconf_backend.log` for debugging.
    -   **Argument Passing**: It receives the `Kconfig` path from the wrapper's initial arguments.
    -   **Execution**: It executes the compiled Rust backend binary: `target/release/wconf`, passing the `Kconfig` path to it as a command-line argument.
3.  **Wait and Cleanup**:
    -   The main C wrapper process now waits for the backend process (the Tauri application window) to terminate.
    -   When the user closes the Tauri window, the backend process exits. The `waitpid` call in the wrapper unblocks.
    -   The wrapper then sends a `SIGTERM` signal to the frontend process (`npm run dev`), killing it.
    -   Signal handlers ensure that if the wrapper itself is killed, it attempts to clean up its children.

## 4. Backend Initialization (Rust)

The Rust application starts up and prepares to serve the UI and handle frontend requests.

1.  **Argument Parsing**: In `main.rs`, the `main` function retrieves the command-line arguments. It takes the last argument as the `kconfig_path` (e.g., `../../../Kconfig`).
2.  **State Management**: It creates a shared `KconfigState` struct and stores the `kconfig_path` within it. This state is managed by Tauri and is accessible to all commands.
3.  **Tauri Setup**:
    -   The `tauri::Builder` is initialized.
    -   The `KconfigState` is registered with `.manage()`.
    -   All functions marked with `#[tauri::command]` (e.g., `load_kconfig`, `set_kconfig_option`) are registered in the `.invoke_handler()`.
    -   The application runs, creating the webview window.

## 5. Frontend Initialization and Data Fetching

Once the backend is running, the frontend is loaded into the webview and begins its setup process.

1.  **Load**: The `index.html` file is served by the Vite dev server and loaded into the webview.
2.  **Script Execution**: The `<script type="module" src="/src/main.js"></script>` tag executes the main JavaScript file.
3.  **Tauri API Handshake**: The script sets up a listener for the `tauri://window-created` event. This is a critical step that ensures the frontend does not try to communicate with the backend before the Tauri API (`window.__TAURI__`) has been injected into the webview.
4.  **Initial Data Request**:
    -   When the `tauri://window-created` event fires, the `setupApp` function is called.
    -   This immediately calls `loadKconfigData()`.
    -   `loadKconfigData` makes the first call to the backend: `await invoke("load_kconfig");`. **No arguments are sent.**

## 6. Data Processing and Response (Backend)

The backend receives the `load_kconfig` request and begins the parsing process. **This is the current point of failure.**

1.  **Command Execution**: The `load_kconfig` function in `main.rs` is triggered.
2.  **Path Retrieval**: It locks the shared `KconfigState` and retrieves the `kconfig_path` that was stored at startup.
3.  **Parsing**: It calls `KconfigParser::parse_file()` with the retrieved path.
    -   Inside `kconfig.rs`, `parse_file` reads the file content.
    -   It calls the external `nom_kconfig::parse_kconfig` function to parse the text into a raw AST. **The application currently hangs here and never returns.**
    -   *If it were to succeed*, it would then call `convert_kconfig_to_nodes` to transform the raw AST into the `Vec<KconfigNode>` structure that the frontend expects.
4.  **Response**: The `load_kconfig` function returns the `Vec<KconfigNode>` to the frontend. Tauri serializes this into a JSON payload.

## 7. Frontend Rendering

If the backend call were to succeed, the frontend would render the received data.

1.  **Receive Data**: The `await invoke("load_kconfig")` promise in `main.js` resolves, and the `kconfigTree` variable is populated with the AST from the backend.
2.  **Render**: The `renderTree` function is called. It recursively walks the `kconfigTree` and generates the corresponding HTML elements.
3.  **Update DOM**: The "Loading Kconfig tree..." message is replaced with the newly generated HTML tree, making the configuration options visible to the user.