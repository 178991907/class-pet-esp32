import { createRouter, createWebHistory } from 'vue-router'
import Home from '@/pages/Home.vue'
import PetPreview from '@/pages/PetPreview.vue'
import StudentFeatures from '@/pages/StudentFeatures.vue'
import DeviceSettingsPage from '@/pages/DeviceSettingsPage.vue'
import DeviceManagementPage from '@/pages/DeviceManagementPage.vue'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    { path: '/', name: 'home', component: Home },
    { path: '/preview', name: 'preview', component: PetPreview },
    { path: '/devices', name: 'devices', component: DeviceManagementPage },
    { path: '/student/:id/features', name: 'student-features', component: StudentFeatures },
    { path: '/student/:id/settings', name: 'student-settings', component: DeviceSettingsPage }
  ]
})

export default router