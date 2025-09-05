#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>  // For PATH_MAX
#include <libgen.h>  // For dirname()
#include <sys/stat.h> // For stat()

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

    // Parse command line arguments
    int development_mode = 0;
    const char *kconfig_path = NULL;
    const char *config_path = NULL;
    
    // Check if DEVELOPMENT_MODE was defined at compile time
    #ifdef DEVELOPMENT_MODE
    development_mode = 1;
    printf("[WRAPPER] Development mode enabled via compile flag\n");
    #endif
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dev") == 0) {
            development_mode = 1;
            printf("[WRAPPER] Development mode enabled via command line\n");
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (argv[i][0] != '-') {
            kconfig_path = argv[i];
        }
    }
    
    if (!kconfig_path) {
        fprintf(stderr, "[WRAPPER ERROR] Missing Kconfig path argument\n");
        fprintf(stderr, "[WRAPPER ERROR] Usage: wconf [--dev] [--config <config_file>] <Kconfig_path>\n");
        return 1;
    }
    
    // Default config path if not specified
    if (!config_path) {
        config_path = ".config";
    }
    
    printf("[WRAPPER] Using Kconfig path: %s\n", kconfig_path);
    printf("[WRAPPER] Mode: %s\n", development_mode ? "Development" : "Production");

    const char *wconf_app_dir = "scripts/kconfig/wconf-app";
    
    if (development_mode) {
        // --- Development Mode: Start Vite Dev Server ---
        frontend_pid = fork();
        if (frontend_pid == -1) {
            perror("[WRAPPER ERROR] Failed to fork frontend process");
            return 1;
        }

        if (frontend_pid == 0) {
            // Child process: Start the frontend dev server
            printf("[WRAPPER] Starting development server in %s\n", wconf_app_dir);
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
        printf("[WRAPPER] Waiting for development server to start...\n");
        sleep(5); // Adjust as needed
    } else {
        // --- Production Mode: Use Pre-built Frontend ---
        printf("[WRAPPER] Using pre-built frontend assets\n");
        
        // Verify dist directory exists
        char dist_path[PATH_MAX];
        snprintf(dist_path, sizeof(dist_path), "%s/dist", wconf_app_dir);
        
        struct stat sb;
        if (stat(dist_path, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
            fprintf(stderr, "[WRAPPER ERROR] Pre-built frontend assets not found in %s\n", dist_path);
            fprintf(stderr, "[WRAPPER ERROR] Please run 'make assets' in %s to build the frontend assets\n", wconf_app_dir);
            return 1;
        }
    }

    // --- Execute Tauri Backend ---
    printf("[WRAPPER] Starting backend process...\n");
    
    // Get absolute path to the binary
    char cwd[4096];  // Use a fixed size buffer instead of PATH_MAX
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("[WRAPPER ERROR] getcwd() failed");
        return 1;
    }
    
    // Try release build in release mode, debug build in debug mode
    const char *release_path = "scripts/kconfig/wconf-app/target/release/wconf";
    const char *debug_path = "scripts/kconfig/wconf-app/target/debug/wconf";
    const char *bin_paths[3] = {0};
    
    // Check if we're in debug mode by looking for DEBUG environment variable
    const char *debug_env = getenv("DEBUG");
    int debug_mode = (debug_env && strcmp(debug_env, "1") == 0);
    
    // Set search order based on debug mode
    if (debug_mode) {
        printf("[WRAPPER] Debug mode enabled, preferring debug build\n");
        bin_paths[0] = debug_path;
        bin_paths[1] = release_path;
    } else {
        printf("[WRAPPER] Release mode, preferring release build\n");
        bin_paths[0] = release_path;
        bin_paths[1] = debug_path;
    }
    
    const char *rust_binary = NULL;
    for (int i = 0; i < 2 && bin_paths[i] != NULL; i++) {
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", cwd, bin_paths[i]);
        printf("[WRAPPER] Checking for binary at: %s\n", full_path);
        if (access(full_path, X_OK) == 0) {
            rust_binary = strdup(full_path);
            printf("[WRAPPER] Found binary: %s\n", rust_binary);
            break;
        } else {
            printf("[WRAPPER] Binary not found or not executable: %s\n", full_path);
        }
    }
    
    if (!rust_binary) {
        fprintf(stderr, "[WRAPPER ERROR] Rust binary not found or not executable.\n");
        return 1;
    }
    
    // Check if Kconfig file exists
    struct stat st;
    if (stat(kconfig_path, &st) != 0) {
        fprintf(stderr, "[WRAPPER ERROR] Kconfig file not found: %s (%s)\n", 
                kconfig_path, strerror(errno));
        free((void*)rust_binary);
        return 1;
    }
    
    // Get absolute paths to the .config file
    char config_abs_path[4096];
    
    // First check if a .config file was explicitly provided
    const char *config_file_path = getenv("KCONFIG_CONFIG");
    if (!config_file_path) {
        // Default to .config in the current directory
        config_file_path = ".config";
    }
    
    // Get absolute path to the config file
    if (realpath(config_path, config_abs_path) == NULL) {
        // If .config doesn't exist, create an empty one
        if (errno == ENOENT) {
            printf("[WRAPPER] No .config file found, creating an empty one\n");
            FILE *f = fopen(config_file_path, "w");
            if (f) fclose(f);
            if (realpath(config_file_path, config_abs_path) == NULL) {
                perror("[WRAPPER ERROR] Failed to create .config file");
                free((void*)rust_binary);
                return 1;
            }
        } else {
            perror("[WRAPPER ERROR] Failed to get absolute path to .config");
            free((void*)rust_binary);
            return 1;
        }
    }
    
    // Get the directory containing the .config file
    char config_dir[4096];
    strncpy(config_dir, config_abs_path, sizeof(config_dir));
    char *last_slash = strrchr(config_dir, '/');
    if (last_slash) {
        *last_slash = '\0';  // Truncate at the last slash
    }
    
    printf("[WRAPPER] Using config file: %s\n", config_abs_path);
    
    // Set up logging for the backend
    char log_path[4096];
    snprintf(log_path, sizeof(log_path), "%s/scripts/kconfig/wconf-app/wconf_backend.log", cwd);
    
    // Fork to run the backend
    printf("[WRAPPER] Forking backend process...\n");
    fflush(stdout);
    
    // Set environment variables for the backend
    setenv("RUST_LOG", "debug,wconf=trace,tao=debug,winit=debug,tauri=debug", 1);
    setenv("RUST_BACKTRACE", "1", 1);
    setenv("KCONFIG_CONFIG", config_abs_path, 1);  // Pass config path to backend
    
    // Create pipes for capturing output
    int stdout_pipe[2];
    int stderr_pipe[2];
    
    if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
        perror("[WRAPPER ERROR] Failed to create pipes");
        free((void*)rust_binary);
        return 1;
    }
    
    backend_pid = fork();
    if (backend_pid == -1) {
        perror("[WRAPPER ERROR] Failed to fork backend process");
        free((void*)rust_binary);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return 1;
    }
    
    if (backend_pid == 0) {
        // Child process - execute the backend
        close(stdout_pipe[0]);  // Close read end
        close(stderr_pipe[0]);  // Close read end
        
        // Redirect stdout and stderr to pipes
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        
        // Close the write ends as they've been duplicated
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        // Execute the backend with the .config file
        printf("[BACKEND] Starting backend: %s\n", rust_binary);
        printf("[BACKEND] Config file: %s\n", config_abs_path);
        fflush(stdout);
        
        // Set KCONFIG_CONFIG environment variable for the backend
        setenv("KCONFIG_CONFIG", config_abs_path, 1);
        
        // Pass the config file path as an argument
        char *backend_argv[] = {(char*)rust_binary, config_abs_path, NULL};
        execv(rust_binary, backend_argv);
        
        // If we get here, execv failed
        perror("[BACKEND] Failed to execute backend");
        _exit(1);
    } else {
        // Parent process
        close(stdout_pipe[1]);  // Close write ends
        close(stderr_pipe[1]);
        
        printf("[WRAPPER] Forked backend process with PID: %d\n", backend_pid);
        printf("[WRAPPER] Backend output will be logged to: %s\n", log_path);
        fflush(stdout);
        
        // Open log file
        FILE *log_file = fopen(log_path, "a");
        if (!log_file) {
            perror("[WRAPPER ERROR] Failed to open log file");
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            free((void*)rust_binary);
            return 1;
        }
        
        // Log header
        fprintf(log_file, "\n=== Starting backend at %s with config: %s ===\n\n", 
                rust_binary, config_abs_path);
        fflush(log_file);
        
        // Set pipes to non-blocking
        fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);
        
        char buffer[4096];
        ssize_t count;
        int status;
        int exit_status = -1;
        
        // Main loop to read from pipes
        while (1) {
            // Check for stdout data
            while ((count = read(stdout_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[count] = '\0';
                fputs(buffer, log_file);
                fflush(log_file);
            }
            
            // Check for stderr data
            while ((count = read(stderr_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[count] = '\0';
                fputs(buffer, log_file);
                fflush(log_file);
            }
            
            // Check if backend process has exited
            pid_t result = waitpid(backend_pid, &status, WNOHANG);
            
            if (result == -1) {
                perror("[WRAPPER] waitpid failed");
                break;
            } else if (result > 0) {
                // Process exited
                if (WIFEXITED(status)) {
                    exit_status = WEXITSTATUS(status);
                    fprintf(log_file, "\n=== Backend exited with status %d ===\n", exit_status);
                } else if (WIFSIGNALED(status)) {
                    fprintf(log_file, "\n=== Backend killed by signal %d ===\n", WTERMSIG(status));
                }
                break;
            }
            
            // Small delay to prevent busy waiting
            usleep(10000);  // 10ms
        }
        
        // Read any remaining data
        while ((count = read(stdout_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[count] = '\0';
            fputs(buffer, log_file);
        }
        while ((count = read(stderr_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[count] = '\0';
            fputs(buffer, log_file);
        }
        
        // Clean up
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        fclose(log_file);
        
        if (exit_status != 0) {
            fprintf(stderr, "[WRAPPER] Backend process failed with status %d. Check %s for details.\n", 
                    exit_status, log_path);
            free((void*)rust_binary);
            return 1;
        }
    }
    // Wait for the frontend to exit
    int frontend_status;
    waitpid(frontend_pid, &frontend_status, 0);
    
    // Clean up
    free((void*)rust_binary);
    
    return 0;
}
