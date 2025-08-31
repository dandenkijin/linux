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
            chdir("../../.."); // Change back to kernel root
        } else {
            fprintf(stderr, "Failed to change to wconf directory\n");
            return 1;
        }
    }
    
    // Execute the wconf binary with the same arguments
    argv[0] = wconf_path;
    execv(wconf_path, argv);
    
    // If we get here, execv failed
    perror("Failed to execute wconf");
    return 1;
}