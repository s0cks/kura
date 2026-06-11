<template>
  <div class="code-tabs">
    <div class="tab-content">
      <slot :name="languages[activeIndex]"></slot>
    </div>
    <div class="tabs">
      <button 
        v-for="(lang, index) in languages" 
        :key="index"
        :class="{ active: activeIndex === index }"
        @click="activeIndex = index"
      >
        {{ lang }}
      </button>
    </div>
  </div>
</template>

<script setup>
import { ref, defineProps } from 'vue'

const props = defineProps({
  languages: {
    type: Array,
    required: true
  }
})

const activeIndex = ref(0)
</script>

<style scoped>
.code-tabs {
  margin: 1rem 0;
}
.tabs {
  display: flex;
  gap: 0.5rem;
  background: var(--vp-c-bg-soft);
  padding: 0.25rem;
  border-radius: 6px;
  width: fit-content;
}
.tabs button {
  padding: 0.25rem 0.75rem;
  border-radius: 4px;
  font-size: 0.9rem;
  border: none;
  background: transparent;
  color: var(--vp-c-text-2);
  cursor: pointer;
}
.tabs button.active {
  background: var(--vp-c-bg);
  color: var(--vp-c-brand);
  font-weight: 500;
}
</style>
