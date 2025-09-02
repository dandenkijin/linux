import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Vite configuration for Tauri
// https://vitejs.dev/config/

export default defineConfig({
  // Prevent vite from obscuring Rust errors
  clearScreen: false,
  
  // Tauri expects a fixed port, fail if that port is not available
  server: {
    port: 5173,
    strictPort: true,
    open: false, // Don't open browser automatically
    fs: {
      // Allow serving files from one level up from the package root
      allow: ['..', '../../..'],
    },
  },
  
  // Environment variables
  envPrefix: ['VITE_', 'TAURI_'],
  
  // Build configuration
  build: {
    target: process.env.TAURI_PLATFORM === 'windows' ? 'chrome105' : 'safari13',
    minify: !process.env.TAURI_DEBUG ? 'esbuild' : false,
    sourcemap: !!process.env.TAURI_DEBUG,
    outDir: 'dist',
    emptyOutDir: true,
    rollupOptions: {
      input: {
        main: resolve(__dirname, 'index.html')
      },
      output: {
        entryFileNames: 'assets/[name]-[hash].js',
        chunkFileNames: 'assets/[name]-[hash].js',
        assetFileNames: 'assets/[name]-[hash][extname]'
      }
    }
  },
  
  // Public directory for static assets
  publicDir: 'public',
  
  // Resolve aliases
  resolve: {
    alias: {
      '@': resolve(__dirname, './src'),
      '@tauri-apps/api': '@tauri-apps/api/dist/tauri',
    },
  },
  
  // Base public path
  base: '/',
  
  // Optimize deps for better performance
  optimizeDeps: {
    // Add any dependencies that should be pre-bundled
    include: ['@tauri-apps/api']
  }
});
