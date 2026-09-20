import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      // deck.gl / map tooling expect mapbox-gl; we use maplibre instead
      'mapbox-gl': 'maplibre-gl'
    }
  },
  server: {
    host: true,
    port: 3000,
    strictPort: true,
    watch: {
      usePolling: true
    }
  }
})
