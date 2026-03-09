import path from "path";
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

const repoRoot = path.resolve(__dirname, "..");
const webViewerRoot = path.resolve(repoRoot, "web_viewer");

export default defineConfig({
    root: webViewerRoot,
    appType: "mpa",
    plugins: [react()],
    server: {
        open: "/index.html",
        watch: {
            usePolling: true,
            interval: 120,
            awaitWriteFinish: {
                stabilityThreshold: 120,
                pollInterval: 60
            }
        }
    },
    build: {
        outDir: path.resolve(repoRoot, "build/web_viewer_dist"),
        emptyOutDir: true
    }
});
