import { createRouter, createWebHistory } from 'vue-router'
import Home from '@/pages/Home.vue'
import PetPreview from '@/pages/PetPreview.vue'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    { path: '/', name: 'home', component: Home },
    { path: '/preview', name: 'preview', component: PetPreview }
  ]
})

export default router