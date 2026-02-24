/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        term_black: "#000000",
        term_gray: "#111111",
        term_border: "#333333",
        term_cyan: "#00f2ea",
        term_amber: "#ffbf00",
        term_green: "#00ff00",
        term_red: "#ff0000",
      },
      fontFamily: {
        mono: ['"JetBrains Mono"', 'Consolas', 'monospace'],
      }
    },
  },
  plugins: [],
}