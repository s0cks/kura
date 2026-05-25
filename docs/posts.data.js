import { createContentLoader } from 'vitepress'

export default createContentLoader('posts/*.md', {
  transform(raw) {
    return raw
      .map(({ url, frontmatter }) => ({
        title: frontmatter.title,
        url,
        date: frontmatter.date,
        tags: frontmatter.tags || [],
        description: frontmatter.description
      }))
      .sort((a, b) => +new Date(b.date) - +new Date(a.date))
  }
})
