/// <reference types="vite/client" />

// Type definitions for Vite environment variables
declare const __APP_VERSION__: string;

interface ImportMetaEnv {
  readonly VITE_APP_TITLE: string;
  // Add other environment variables here as needed
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
