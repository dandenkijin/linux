// SPDX-License-Identifier: GPL-2.0
// Build script for the Tauri-based kernel configuration tool

use std::path::PathBuf;

fn main() {
    // This will embed the frontend files in the binary
    let frontend_dist = PathBuf::from("dist");
    
    // Tell Cargo to re-run this build script if any frontend files change
    println!("cargo:rerun-if-changed=dist");
    
    // Set the environment variable that Tauri will use to find the frontend
    println!("cargo:rustc-env=TAURI_DIST_DIR={}", frontend_dist.display());
    
    // Build the Tauri application
    tauri_build::build();
}
