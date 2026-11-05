import { defineConfig } from "vite";

// Tauri expects a fixed dev port and serves the built assets from ../dist.
export default defineConfig({
  clearScreen: false,
  server: { port: 5173, strictPort: true },
  build: { outDir: "dist", target: "es2021", emptyOutDir: true },
});
