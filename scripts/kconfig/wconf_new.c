// SPDX-License-Identifier: GPL-2.0
// C wrapper for the Tauri-based kernel configuration tool (wconf)
// This wrapper simply executes the Rust-based wconf application

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

int main(int argc, char **argv)
{
    char *wconf_path;
    struct stat st;
    
    // Try to find the wconf binary in the same directory as this wrapper
    wconf_path = getenv("WCONF_PATH");
    if (!wconf_path) {
        wconf_path = "./scripts/kconfig/wconf-app/target/release/wconf";
    }
    
    // Check if the wconf binary exists
    if (stat(wconf_path, &st) != 0) {
        // Try to build it if it doesn't exist
        fprintf(stderr, "wconf binary not found at %s, attempting to build...\n", wconf_path);
        
        // Change to the wconf directory and build with custom-protocol feature
        if (chdir("scripts/kconfig/wconf-app") == 0) {
            int result = system("cargo build --release --features custom-protocol");
            if (result != 0) {
                fprintf(stderr, "Failed to build wconf\n");
                return 1;
    }
            }
            chdir("../../.."); // Change back to kernel root
        } else {
            fprintf(stderr, "Failed to change to wconf directory\n");
            return 1;
    }
        }
    }
    
    // Execute the wconf binary with the same arguments
    char **new_argv;
    int i;
    
    // Create a new argv array with the wconf binary path and all original arguments
    new_argv = malloc((argc + 1) * sizeof(char *));
    if (!new_argv) {
        fprintf(stderr, "Failed to allocate memory for argv\n");
        return 1;
    }
    }
    
    // Set the first argument to the wconf binary path
    new_argv[0] = wconf_path;
    
    // Copy all original arguments (skip argv[0] which is the wrapper program name)
    for (i = 1; i < argc; i++) {
        new_argv[i] = argv[i];
    }
    new_argv[argc] = NULL; // Null-terminate the array
    
    // Execute the wconf binary with all original arguments
    execv(wconf_path, new_argv);
    execv(wconf_path, argv);
    
    // If we get here, execv failed
    perror("Failed to execute wconf");
    free(new_argv);
    return 1;
    }
}