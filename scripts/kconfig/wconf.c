#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

// Global variables to hold child process IDs
pid_t frontend_pid = -1;
pid_t backend_pid = -1;

void cleanup_children(void) {
    // Terminate children if they are running
    if (backend_pid > 0) {
        printf("[WRAPPER] Terminating backend process (PID %d)\n", backend_pid);
        kill(backend_pid, SIGTERM);
    }
    if (frontend_pid > 0) {
        printf("[WRAPPER] Terminating frontend process (PID %d)\n", frontend_pid);
        kill(frontend_pid, SIGTERM);
    }
    // Wait for all children to prevent zombies
    while(wait(NULL) > 0);
    printf("[WRAPPER] Cleanup complete.\n");
}

void handle_signal(int sig) {
    printf("[WRAPPER] Received signal %d, cleaning up...\n", sig);
    exit(1); // Exit will trigger the atexit handler
}

int main(int argc, char *argv[]) {
    // Force unbuffered output for immediate debugging
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // Register cleanup function and signal handlers
    atexit(cleanup_children);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);

    printf("[WRAPPER] Starting with %d arguments\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("[WRAPPER] Arg %d: '%s'\n", i, argv[i]);
    }

    // Validate minimum required arguments
    if (argc < 2) {
        fprintf(stderr, "[WRAPPER ERROR] Missing Kconfig path argument\n");
        fprintf(stderr, "[WRAPPER ERROR] Usage: wconfig [options] <Kconfig_path>\n");
        return 1;
    }

    // Kconfig path is always the last argument
    const char *kconfig_path = argv[argc-1];
    printf("[WRAPPER] Using Kconfig path: %s\n", kconfig_path);

    // Verify Kconfig file exists
    struct stat sb;
    if (stat(kconfig_path, &sb) != 0) {
        int err = errno;
        fprintf(stderr, "[WRAPPER ERROR] Kconfig not found: %s (errno=%d)\n", 
                kconfig_path, err);
        perror("stat failed");
        return 1;
    }

    // --- Start Frontend Development Server ---
    const char *wconf_app_dir = "scripts/kconfig/wconf-app";
    
    frontend_pid = fork();
    if (frontend_pid == -1) {
        perror("[WRAPPER ERROR] Failed to fork frontend process");
        return 1;
    }

    if (frontend_pid == 0) {
        // Child process: Start the frontend server
        printf("[WRAPPER] Changing directory to %s\n", wconf_app_dir);
        if (chdir(wconf_app_dir) != 0) {
            perror("[WRAPPER ERROR] Failed to change directory to wconf-app");
            exit(1);
        }
        
        printf("[WRAPPER] Executing 'npm run dev'...\n");
        execlp("npm", "npm", "run", "dev", NULL);
        
        // If execlp returns, it failed
        perror("[WRAPPER ERROR] Failed to execute 'npm run dev'");
        exit(1);
    }

    // Parent process: Wait a moment for the server to start
    printf("[WRAPPER] Waiting for frontend server to start...\n");
    sleep(5); // Adjust as needed

    // --- Execute Tauri Backend ---
    const char *rust_binary = "scripts/kconfig/wconf-app/target/debug/wconf";
    if (access(rust_binary, X_OK) != 0) {
        fprintf(stderr, "[WRAPPER ERROR] Rust binary not found or not executable: %s\n", rust_binary);
        return 1;
    }

    backend_pid = fork();
    if (backend_pid == -1) {
        perror("[WRAPPER ERROR] Failed to fork backend process");
        return 1;
    }

    if (backend_pid == 0) { // Child process for backend
        // This process MUST run from the kernel root, so we DO NOT chdir.

        // Redirect stdout and stderr to a log file inside the wconf-app directory.
        if (freopen("scripts/kconfig/wconf-app/wconf_backend.log", "w", stdout) == NULL) {
            perror("[BACKEND-CHILD] freopen stdout failed");
            exit(127);
        }
        if (freopen("scripts/kconfig/wconf-app/wconf_backend.log", "a", stderr) == NULL) {
            perror("[BACKEND-CHILD] freopen stderr failed");
            exit(127);
        }
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);

        printf("[WRAPPER] Executing Rust binary from kernel root.\n");

        // The path to the binary is relative to the kernel root.
        const char *rust_binary_path = "scripts/kconfig/wconf-app/target/debug/wconf";

        // The kconfig_path argument is already relative to the kernel root.
        char * const backend_argv[] = { (char*)rust_binary_path, (char*)kconfig_path, NULL };
        execv(rust_binary_path, backend_argv);

        // If execv returns, it's an error
        perror("[WRAPPER ERROR] Failed to execute Rust binary");
        exit(127);
    }

    // Parent process: Wait for the backend (UI) to close
    printf("[WRAPPER] Waiting for backend process (PID %d) to exit...\n", backend_pid);
    int status;
    waitpid(backend_pid, &status, 0);
    printf("[WRAPPER] Backend process exited. Cleaning up frontend.\n");
    backend_pid = -1; // Mark as terminated

    // The atexit handler will now clean up the frontend process.
    return 0;
}