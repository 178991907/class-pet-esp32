import { defineStore } from 'pinia'
import { ref } from 'vue'
import type { Device } from '@/types'
import { useAuth } from '@/composables/useAuth'
import { useToast } from '@/composables/useToast'

export const useDeviceStore = defineStore('device', () => {
  const { api } = useAuth()
  const toast = useToast()

  const devices = ref<Device[]>([])
  const unboundDevices = ref<Device[]>([])
  const loading = ref(false)

  async function loadDevices() {
    loading.value = true
    try {
      const res = await api.get('/devices')
      devices.value = res.data.devices || []
    } catch (err) {
      console.error('加载设备列表失败:', err)
    } finally {
      loading.value = false
    }
  }

  async function loadUnboundDevices() {
    try {
      const res = await api.get('/devices/unbound')
      unboundDevices.value = res.data.devices || []
    } catch (err) {
      console.error('加载未绑定设备失败:', err)
    }
  }

  async function registerDevice(deviceId: string, name?: string, classId?: string) {
    try {
      await api.post('/devices/register', { device_id: deviceId, name: name || deviceId, class_id: classId || null })
      toast.success('设备已登记')
      await loadDevices()
      return true
    } catch (err: any) {
      toast.error(err?.response?.data?.error || '登记设备失败')
      return false
    }
  }

  async function updateDevice(deviceId: string, payload: { student_id?: string | null, name?: string, class_id?: string }) {
    try {
      await api.put(`/devices/${encodeURIComponent(deviceId)}`, payload)
      toast.success('设备已更新')
      await loadDevices()
      return true
    } catch (err: any) {
      toast.error(err?.response?.data?.error || '更新设备失败')
      return false
    }
  }

  async function deleteDevice(deviceId: string) {
    try {
      await api.delete(`/devices/${encodeURIComponent(deviceId)}`)
      toast.success('设备已删除')
      await loadDevices()
      return true
    } catch (err: any) {
      toast.error(err?.response?.data?.error || '删除设备失败')
      return false
    }
  }

  async function loadDeviceSettings(deviceId: string) {
    const res = await api.get(`/devices/${encodeURIComponent(deviceId)}`)
    return {
      device: res.data.device,
      settings: res.data.settings || {}
    }
  }

  async function saveDeviceSettings(deviceId: string, settings: Record<string, any>) {
    // 复用 /device/settings 的设备级写入 (带 device_id 参数)
    await api.post(`/device/settings?device_id=${encodeURIComponent(deviceId)}`, settings)
  }

  return {
    devices,
    unboundDevices,
    loading,
    loadDevices,
    loadUnboundDevices,
    registerDevice,
    updateDevice,
    deleteDevice,
    loadDeviceSettings,
    saveDeviceSettings
  }
})
