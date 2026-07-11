// 班级宠物园 · 轻量级进程内事件总线 + SSE 广播中枢
// 用途：设备端/服务端的评价、升级、语音会话等事件实时推送给 Web 管理端，
//       实现「老师在网页上实时看到设备互动动态」的 P3 需求。
//
// 设计要点：
//   - 纯内存、单进程。对于多实例部署(如 Vercel 多函数)不保证跨实例，
//     但本项目生产为单 VPS Node 进程，足够使用。
//   - 客户端通过 GET /api/events 建立 SSE 长连接，按事件名订阅。
import { EventEmitter } from 'events'

const bus = new EventEmitter()
bus.setMaxListeners(0)

// 当前在线的 SSE 客户端响应对象集合
const clients = new Set()

// 订阅：把一个 express res 注册为 SSE 客户端，返回取消订阅函数
export function subscribe(res) {
  clients.add(res)
  return () => clients.delete(res)
}

export function clientCount() {
  return clients.size
}

// 发布事件：进程内广播 + 推送给所有 SSE 客户端
// event = { type, payload, timestamp }
export function publishEvent(type, payload = {}) {
  const event = { type, payload, timestamp: Date.now() }
  bus.emit('event', event)
  const frame = `event: ${type}\ndata: ${JSON.stringify(event)}\n\n`
  for (const res of clients) {
    try {
      res.write(frame)
    } catch {
      // 连接已断开，下一次心跳会清理
    }
  }
  return event
}

// 供路由按需监听（当前未使用，保留扩展）
export function onEvent(listener) {
  bus.on('event', listener)
  return () => bus.off('event', listener)
}

export default { subscribe, publishEvent, clientCount, onEvent }
