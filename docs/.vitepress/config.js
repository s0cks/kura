import { defineConfig } from 'vitepress'

export default defineConfig({
  base: '/kura/', 

  // 2. Global Site SEO Meta
  title: 'Kura',
  description: 'A blog for Kura',

  // 3. Default Theme Customizations
  themeConfig: {
    // Top Right Navigation Links
    nav: [
      { text: 'Home', link: '/' },
      { text: 'Posts', link: '/posts/hello-world' }
    ],

    // Sidebar Links
    sidebar: [
      {
        text: 'Recent Posts',
        items: []
      }
    ],

    // Social Media Quick-links
    socialLinks: [
      { icon: 'github', link: 'https://github.com/s0cks' }
    ]
  }
})
