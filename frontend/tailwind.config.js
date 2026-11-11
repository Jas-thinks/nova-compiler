/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        ink: { 950: '#0b0d10', 900: '#0f1217', 850: '#13171d', 800: '#181d25', 700: '#222934', 600: '#2d3644', 500: '#46536a' },
        fg: { DEFAULT: '#d5dbe6', dim: '#98a2b3', mute: '#667085' },
        nova: { DEFAULT: '#7b93ff', soft: '#7b93ff22', strong: '#9db0ff' },
        ok: '#3fb68b',
        bad: '#f2555a',
        warn: '#e5a93a',
      },
      fontFamily: {
        sans: ['Inter', 'ui-sans-serif', 'system-ui', '-apple-system', 'Segoe UI', 'Roboto', 'sans-serif'],
        mono: ['"JetBrains Mono"', '"Fira Code"', 'ui-monospace', 'SFMono-Regular', 'Menlo', 'Consolas', 'monospace'],
      },
      fontSize: { '2xs': ['0.6875rem', '1rem'] },
    },
  },
  plugins: [],
}
