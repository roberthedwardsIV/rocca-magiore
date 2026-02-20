/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        void: "#0c0c0c",       // Pure Black
        parchment: "#f5f5f5",  // Nice Tan
        nautical: "#001f3f",   // Deep Navy Blue
        slate: "#2d2d2d",      // Subtle Gray
        paper: "#d4c3a3",      // Off-White
      },
      fontFamily: {
        mono: ['"JetBrains Mono"', 'monospace'],
        serif: ['"Georgia"', 'serif'], // For headers to give that "Legacy" feel
      }
    },
  },
  plugins: [],
}