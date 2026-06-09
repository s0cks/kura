import DefaultTheme from 'vitepress/theme'
import BlogIndex from '../components/BlogIndex.vue'
import './custom.css'

export default {
  extends: DefaultTheme,
  head: [
    [
      'link',
      {
        rel: 'preconnect',
        href: 'https://fonts.googleapis.com'
      }
    ],
    [
      'link',
      {
        rel: 'stylesheet',
        href: 'https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&family=JetBrains+Mono:wght@400;500;600&display=swap'
      }
    ]
  ],
  appearance: 'dark',
  enhanceApp({ app }) {
    app.component('BlogIndex', BlogIndex)
  }
}
