import { defineConfig } from 'vitepress'
import { generateSidebar } from 'vitepress-sidebar'

export default defineConfig({
  base: '/', 
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
      useTitleFromFrontmatter: true,
      collapsed: false,
      capitalizeEachWords: true,
      basePath: '/posts/',
    }),
    socialLinks: [
      { icon: 'github', link: 'https://github.com/s0cks/kura' }
    ]
  }
})
