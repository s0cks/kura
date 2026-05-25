import { defineConfig } from 'vitepress'
import { generateSidebar } from 'vitepress-sidebar'

export default defineConfig({
  base: '/kura/', 
  title: 'Kura',
  description: 'A blog for Kura',
  themeConfig: {
    nav: [
      { text: 'Home', link: '/' },
    ],
    sidebar: generateSidebar({
      documentRootPath: "docs",
      resolvePath: "/posts/", 
      useTitleFromFileHeading: true,
      convertEndingToHTML: false,
      basePath: '/posts/',
    }),
    socialLinks: [
      { icon: 'github', link: 'https://github.com/s0cks/kura' }
    ]
  }
})
