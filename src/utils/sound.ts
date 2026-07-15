// 轻量提示音工具：用 Web Audio API 实时合成，无需任何音频文件。
// 用户操作（添加成功等）触发，配合不同功能类型给出不同音效提示。

let ctx: AudioContext | null = null

function getCtx(): AudioContext | null {
  try {
    if (!ctx) {
      const AC = window.AudioContext || (window as any).webkitAudioContext
      if (!AC) return null
      ctx = new AC()
    }
    if (ctx.state === 'suspended') ctx.resume()
    return ctx
  } catch {
    return null
  }
}

// 单个音：freq 频率, start 延迟秒, dur 时长秒, type 波形, gain 音量
function beep(freq: number, start: number, dur: number, type: OscillatorType = 'sine', gain = 0.14) {
  const ac = getCtx()
  if (!ac) return
  const osc = ac.createOscillator()
  const g = ac.createGain()
  osc.type = type
  osc.frequency.value = freq
  const t0 = ac.currentTime + start
  g.gain.setValueAtTime(0.0001, t0)
  g.gain.linearRampToValueAtTime(gain, t0 + 0.012)
  g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur)
  osc.connect(g)
  g.connect(ac.destination)
  osc.start(t0)
  osc.stop(t0 + dur + 0.03)
}

export type ChimeType = 'calendar' | 'checklist' | 'alarm' | 'success'

// 不同类型 → 不同提示音
export function playChime(type: ChimeType) {
  try {
    if (type === 'calendar') {
      // 清脆上行双音（叮-咚）
      beep(880, 0, 0.16, 'sine')
      beep(1175, 0.13, 0.2, 'sine')
    } else if (type === 'checklist') {
      // 短促单音（完成感）
      beep(660, 0, 0.12, 'triangle', 0.12)
    } else if (type === 'alarm') {
      // 闹钟连续三声方波
      beep(784, 0, 0.18, 'square', 0.1)
      beep(784, 0.22, 0.18, 'square', 0.1)
      beep(784, 0.44, 0.24, 'square', 0.1)
    } else {
      // 通用成功（柔和单音）
      beep(988, 0, 0.14, 'sine', 0.1)
    }
  } catch {
    /* 音频不可用时静默忽略 */
  }
}
