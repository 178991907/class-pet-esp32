# 提交前检查清单（硬件相关改动）

> 配合 `CLAUDE.md` 与 `DEVELOPMENT_STANDARD.md` 使用。
> 任何涉及固件引脚 / 音频 / 显示 / 编译配置的提交，合入前必须逐项确认。
> （pre-commit 钩子已自动拦截部分红线，本清单用于人工复核。）

## A. 引脚与音频（最高优先级）

- [ ] 未改动 `Board.h` 的权威引脚（§1），或改动已在 `DEVELOPMENT_STANDARD.md` 登记并验证
- [ ] 麦克风脚仍是 **GPIO6**（`I2S_DIN_PIN`），没有把 GPIO8 当麦克风读
- [ ] 喇叭脚仍是 **GPIO8**（`I2S_DOUT_PIN`）
- [ ] 录音时 `AUDIO_EN_PIN`(GPIO1) = **HIGH**（功放静音）；播放/输出时 = LOW（使能）
- [ ] 录音 / ASR / 唤醒词采样率 = **16000 Hz**（MP3 播放可 44.1k）
- [ ] MCLK 倍频 = **256**
- [ ] ES8311 ADC：`REG14=0x0A` / `REG17=0xF0` / `MIC_GAIN_30DB` 未被改
- [ ] I2S 端口仍是 `I2S_NUM_0`（`Board.h` 的 `I2S_NUM_USE` 未被回退为 `I2S_NUM_1`）
- [ ] ES8311 复用 `I2C_NUM_0`，未重复 `i2c_driver_install`

## B. 显示 / 字库

- [ ] 中文走 TF 卡 `cjk16.bin` + `lv_binfont_create_from_buffer`，字库未编进固件
- [ ] UI 文案无 emoji（不会显示成方块）
- [ ] 背光 `BL=45` 高电平点亮逻辑正确

## C. 编译 / 构建

- [ ] FQBN 含 `PSRAM=opi` 与 `CDCOnBoot=cdc` 与 `PartitionScheme=huge_app`
- [ ] 编译通过，无 LVGL / 音频 / JSON 相关报错（见 `CLAUDE.md` §3 常见错误）
- [ ] 烧录后设备启动无崩溃、串口有正常日志

## D. 唤醒词（若本次涉及）

- [ ] esp-sr 组件经 `EXTRA_COMPONENT_DIRS` 被发现并链接
- [ ] 唤醒词模型已烧录到 `model` 分区（或按标准 §6 的加载方式）
- [ ] `AFE_MEMORY_ALLOC_MORE_PSRAM` 已设（依赖 PSRAM=opi）
- [ ] 仅用 S3 支持的唤醒词（非 `你好小智`）

## E. LVGL 双核

- [ ] 从 `loop()`(Core 1) 的 LVGL 改动都包在 `lock()`/`unlock()` 中
- [ ] LVGL 事件回调内部未重复 lock

## F. 实测（能上机时）

- [ ] 录音：串口日志峰值 **> 200/32767**（麦克风有真实信号）
- [ ] 播放：喇叭能出声（音调测试 / MP3）
- [ ] 录音期间功放确为静音（无啸叫 / 无 “you” 幻觉）
