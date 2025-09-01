// SPDX-License-Identifier: GPL-2.0
// A minimal Vite configuration for the Tauri wconf application.

import { defineConfig } from "vite";

// This minimal configuration relies on Vite's defaults, which are generally
// well-suited for a standard project structure like ours. We only override
// the absolute necessities for Tauri development.
export default defineConfig({
  // Prevent Vite from clearing the terminal screen, which can hide
  // important messages from Tauri.
  clearScreen: false,

  // The server configuration is critical for Tauri's `devPath` to work correctly.
  server: {
    // We must have a predictable port for Tauri to connect to.
    port: 5173,
    // `strictPort` ensures the dev server will fail if the port is already in use,
    // rather than trying another one. This avoids confusion.
    strictPort: true,
  },

  // The build configuration is necessary for `tauri build` to package the app.
  build: {
    // This is the directory where `npm run build` will place the final assets.
    // Tauri reads from here when creating a production build.
    outDir: "./dist",
  },
});
