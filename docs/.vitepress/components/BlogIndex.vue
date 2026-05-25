<script setup>
import { ref, computed } from 'vue'
import { data as posts } from '../../posts.data.js'

const selectedTag = ref(null)

// Format raw string dates (2026-05-25 -> May 25, 2026)
const formatDate = (dateString) => {
  if (!dateString) return ''
  const date = new Date(dateString)
  return date.toLocaleDateString('en-US', {
    year: 'numeric',
    month: 'long',
    day: 'numeric'
  })
}

// Extract all unique tags across all posts
const allTags = computed(() => {
  const tags = new Set()
  posts.forEach(post => post.tags.forEach(tag => tags.add(tag)))
  return Array.from(tags)
})

// Filter posts based on selected tag
const filteredPosts = computed(() => {
  if (!selectedTag.value) return posts
  return posts.filter(post => post.tags.includes(selectedTag.value))
})

const toggleTag = (tag) => {
  selectedTag.value = selectedTag.value === tag ? null : tag
}
</script>

<template>
  <div class="blog-container">
    <!-- Tag Filter Bar -->
    <div class="tags-filter">
      <button 
        v-for="tag in allTags" 
        :key="tag"
        :class="{ active: selectedTag === tag }"
        @click="toggleTag(tag)"
      >
        #{{ tag }}
      </button>
      <button v-if="selectedTag" @click="selectedTag = null" class="clear-btn">
        Clear Filter
      </button>
    </div>

    <!-- Dynamic Post List -->
    <div class="posts-list">
      <article v-for="post in filteredPosts" :key="post.url" class="post-card">
        <h2><a :href="'/kura' + post.url">{{ post.title }}</a></h2>
        <p class="post-meta">{{ formatDate(post.date) }}</p>
        <p class="post-desc">{{ post.description }}</p>
        <div class="post-tags">
          <span v-for="t in post.tags" :key="t" class="tag-badge">#{{ t }}</span>
        </div>
      </article>
    </div>
  </div>
</template>

<style scoped>
.blog-container { margin-top: 2rem; }
.tags-filter { display: flex; gap: 0.5rem; flex-wrap: wrap; margin-bottom: 2rem; }
button { padding: 0.25rem 0.75rem; border-radius: 4px; background: var(--vp-c-bg-mute); border: 1px solid var(--vp-c-divider); cursor: pointer; transition: all 0.2s; }
button.active, button:hover { background: var(--vp-c-brand-1); color: white; border-color: var(--vp-c-brand-1); }
.clear-btn { background: transparent; border: none; color: var(--vp-c-text-2); text-decoration: underline; }
.post-card { padding: 1.5rem 0; border-bottom: 1px solid var(--vp-c-divider); }
.post-card h2 { margin: 0 0 0.5rem 0; font-size: 1.5rem; }
.post-card a { color: var(--vp-c-brand-1); text-decoration: none; font-weight: 600; }
.post-card a:hover { text-decoration: underline; }
.post-meta { font-size: 0.85rem; color: var(--vp-c-text-3); margin-bottom: 0.5rem; }
.post-desc { color: var(--vp-c-text-2); margin-bottom: 0.5rem; }
.tag-badge { font-size: 0.8rem; background: var(--vp-c-bg-alt); padding: 0.1rem 0.4rem; border-radius: 3px; margin-right: 0.5rem; color: var(--vp-c-text-2); }
</style>
