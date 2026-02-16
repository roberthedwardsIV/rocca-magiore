/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        void: "#0a0b10",       // Deep background
        gunmetal: "#14161f",   // Panels
        cyan: "#00f2ea",       // Accents
        crimson: "#ff0055",    // Alerts
        amber: "#ffcc00",      // Warnings
        offwhite: "#e0e0e0"    // Text
      },
      fontFamily: {
        mono: ['ui-monospace', 'SFMono-Regular', 'Menlo', 'Monaco', 'Consolas', 'monospace'],
        sans: ['system-ui', '-apple-system', 'BlinkMacSystemFont', 'Segoe UI', 'Roboto', 'sans-serif']
      }
    },
  },
  plugins: [],
}