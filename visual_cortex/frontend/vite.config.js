import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      // CRITICAL: Redirects mapbox imports to maplibre
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