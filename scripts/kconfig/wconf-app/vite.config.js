// SPDX-License-Identifier: GPL-2.0
// Vite configuration for the Tauri-based kernel configuration tool

import { defineConfig } from "vite";

export default defineConfig({
  // GitHub Pages requires relative paths
  base: "./",

  // Specify the root directory
  root: ".",

  // Output directory for build
  build: {
    outDir: "./dist",
    emptyOutDir: true,
  },

  // Development server configuration
  server: {
    port: 5173,
  },
});
