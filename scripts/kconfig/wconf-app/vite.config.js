import { defineConfig } from 'vite';
import { resolve } from 'node:path';

// https://vitejs.dev/config/
// 'mode' must be explicitly set via --mode flag in npm scripts
// or defaults to 'production' in Vite
// WCONFIG_DEBUG=1 will force development mode
export default defineConfig(({ mode }) => {
  // Development mode is enabled when:
  // 1. Explicitly set via --mode development, or
  // 2. WCONFIG_DEBUG is set to '1'
  const isDev = mode === 'development' || process.env.WCONFIG_DEBUG === '1';
  
  return {
    // Set the base path for production builds
    base: isDev ? '/' : './',
    
    // Prevent Vite from obscuring Rust errors
    clearScreen: false,
    
    // Development server configuration
    server: {
      port: 5173, // Standard Vite dev server port
      strictPort: true,
      fs: {
        // Allow serving files from one level up from the package root
        allow: ['..']
      }
    },
    
    // Environment variables
    envPrefix: ['VITE_', 'WCONFIG_'],
    
    build: {
      // Minify in production only
      minify: isDev ? false : 'esbuild',
      
      // Generate sourcemaps in development
      sourcemap: isDev,
      
      // Output directory
      outDir: 'dist',
      
      // Clean the output directory before building in production
      emptyOutDir: !isDev,
      
      // Ensure assets are copied to the correct location
      assetsInlineLimit: 0,
      
      // Assets directory (relative to outDir)
      assetsDir: 'assets',
      
      // Rollup options
      rollupOptions: {
        // Main entry point
        input: resolve(__dirname, 'index.html'),
        output: {
          entryFileNames: 'assets/[name]-[hash].js',
          chunkFileNames: 'assets/[name]-[hash].js',
          assetFileNames: (assetInfo) => {
            // Handle different asset types
            const info = assetInfo.name.split('.');
            const ext = info[info.length - 1];
            if (['png', 'jpg', 'jpeg', 'gif', 'svg', 'webp'].includes(ext)) {
              return 'assets/images/[name]-[hash][extname]';
            }
            if (ext === 'css') {
              return 'assets/css/[name]-[hash][extname]';
            }
            return 'assets/[name]-[hash][extname]';
          },
        },
        // External dependencies
        external: [
          '@tauri-apps/api/tauri',
          '@tauri-apps/api/event',
          '@tauri-apps/api/window'
        ]
      },
    },
    
    // Dependencies optimization
    optimizeDeps: {
      // Exclude Tauri dependencies
      exclude: ['@tauri-apps/api'],
      // Enable esbuild optimizations for development
      esbuildOptions: {
        // Support for top-level await
        target: 'esnext',
        supported: { 
          bigint: true,
        },
      },
    },
  };
});
