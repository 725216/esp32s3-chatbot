# ESP32-S3 双AI语音对话固件 - 接线指南

## 硬件要求

- **主控**: ESP32-S3-N16R8 (16MB Flash, 8MB PSRAM Octal)
- **开发板**: 任意 ESP32-S3 开发板（需引出 GPIO38、GPIO21）
- **按键**: 2 个轻触按键（PTT + SWITCH）
- **USB 线**: USB-C 数据线（用于供电和刷写固件）

---

## 按键接线图

```
ESP32-S3 开发板 (左侧排针)
┌─────────────────────────────────────┐
│  [3V3]  [GPIO38] PTT 按键 → GND    │  ← 对话键（按住说话）
│  [GND]  [GPIO21] SWITCH 按键 → GND │  ← 切换键（小智↔豆包）
│  [GPIO0] BOOT 键 (板载)            │  ← 重配网键（按住2秒）
│  ...                                │
└─────────────────────────────────────┘
```

### 详细接线

| 功能 | GPIO | 接线方式 | 说明 |
|------|------|---------|------|
| **PTT 对话键** | GPIO38 | 按键一端接 GPIO38，另一端接 GND | 按住说话，松开停止 |
| **SWITCH 切换键** | GPIO21 | 按键一端接 GPIO21，另一端接 GND | 短按一下切换 AI Agent |
| **BOOT 重配网** | GPIO0 | 板载按键（无需接线） | 按住 2 秒进入配网模式 |

### 内部上拉

固件已启用内部上拉电阻（`GPIO_PULLUP_ENABLE`），**无需外接上拉电阻**。

---

## 音频接线（麦克风 + 功放）

### INMP441 I2S 麦克风（录音）

| INMP441 引脚 | ESP32-S3 GPIO | 说明 |
|-------------|--------------|------|
| VDD | 3.3V | 供电 |
| GND | GND | 地 |
| WS | GPIO4 | 帧时钟 (L/R) |
| SCK | GPIO5 | 位时钟 (BCLK) |
| SD | GPIO6 | 数据输出 (DOUT) |
| L/R | GND | 接地=左声道（固件配为单声道左槽） |
| SEL | GND | 接地=SCK×64 模式 |

### MAX98357A I2S 功放（播放，接无源喇叭）

| MAX98357A 引脚 | ESP32-S3 GPIO | 说明 |
|---------------|--------------|------|
| Vin | 5V 或 3.3V | 供电（5V 音量更大） |
| GND | GND | 地 |
| BCLK | GPIO8 | 位时钟 |
| L/R | GPIO7 | 帧时钟 (WS/LRC) |
| DIN | GPIO9 | 数据输入 |
| SD_MODE | 3.3V | 拉高使能（悬空不工作！） |
| GPO1/GPO2/GPO3 | 不接 | 增益配置脚，板内已下拉 |
| Vo+ / Vo- | 喇叭两极 | 无源喇叭 4Ω/8Ω，不分正负 |

```麦克风                              功放
INMP441                          MAX98357A
┌─────────┐                    ┌───────────┐
│ WS  SCK SD│── GPIO4/5/6      │ L/R BCLK DIN│── GPIO7/8/9
│ L/R SEL → GND                  │ SD_MODE → 3V3 │
│ VDD → 3V3  GND → GND           │ Vin → 5V      │
└─────────┘                    │ Vo+ Vo- → 喇叭 │
                                 └───────────┘
```

> 注：麦克风用 I2S 端口 1，功放在端口 0，两者独立互不影响；无 MCLK（16kHz 采样率下不需要）。

### 引脚占用总览

| GPIO | 用途 | 备注 |
|------|------|------|
| 0 | BOOT 键（板载） | 重配网；strapping 引脚，上电时电平决定启动模式 |
| 4 | MIC WS | I2S in |
| 5 | MIC SCK | I2S in |
| 6 | MIC SD | I2S in |
| 7 | 功放 L/R | I2S out |
| 8 | 功放 BCLK | I2S out |
| 9 | 功放 DIN | I2S out |
| 19/20 | USB D-/D+ | 板载 USB，勿动 |
| 21 | SWITCH 切换键 | 外接按键 → GND |
| 35/36/37 | Octal PSRAM 专用 | 板上已占用，勿接任何外设 |
| 38 | PTT 对话键 | 外接按键 → GND |
| 1 | 不可用 | 板载电路拉低，接任何外设均无效（实测） |
| 45/46 | strapping | 尽量空闲 |
| 48 | 板载 RGB LED | 可用但不推荐 |
| 10~18, 41~42, 47 | 空闲 | 可留给屏幕/触摸等后续扩展（见 docs/DISPLAY.md） |

---

## 实物接线示例

```
        ┌──────────────────────────────┐
        │     ESP32-S3-N16R8 开发板     │
        │                              │
        │  [USB-C] ─── USB 线 ──→ 电脑  │
        │                              │
        │  左侧排针:                    │
        │  ┌────────────────────────┐  │
        │  │ 3V3  │ GPIO38 │ ...   │  │
        │  │ GND  │ GPIO21 │ ...   │  │
        │  │ ...  │ ...    │ ...   │  │
        │  └────────────────────────┘  │
        │         │        │           │
        │         ▼        ▼           │
        │    ┌──────┐ ┌──────┐         │
        │    │ PTT  │ │ SW   │         │
        │    │按键  │ │ 按键 │         │
        │    └──┬───┘ └──┬───┘         │
        │       │        │             │
        │       └───┬────┘             │
        │           │                  │
        │          GND                 │
        └──────────────────────────────┘
```

---

## 固件刷写

### 1. 下载固件

从 [GitHub Releases](https://github.com/YOUR_REPO/esp32s3_chatbot/releases) 下载最新固件，或从 GitHub Actions 构建产物下载。

### 2. 安装 esptool

```bash
pip install esptool
```

### 3. 进入下载模式

1. 按住 **BOOT** 键
2. 插入 USB 线（或按 **RST** 键）
3. 松开 **BOOT** 键

### 4. 刷写固件

**完整刷写**（首次或分区表变更时）：

```bash
# Windows
esptool.py --port COM4 --baud 921600 write_flash ^
  0x0 bootloader.bin ^
  0x8000 partition-table.bin ^
  0x10000 esp32s3_chatbot.bin

# Linux/Mac
esptool.py --port /dev/ttyUSB0 --baud 921600 write_flash \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 esp32s3_chatbot.bin
```

**仅更新 App**（保留 NVS WiFi 配置）：

```bash
# Windows
esptool.py --port COM4 --baud 921600 write_flash 0x10000 esp32s3_chatbot.bin

# Linux/Mac
esptool.py --port /dev/ttyUSB0 --baud 921600 write_flash 0x10000 esp32s3_chatbot.bin
```

### 5. 重启设备

按 **RST** 键或重新插拔 USB 线。

---

## 首次使用

### WiFi 配网

1. 上电后，若无已存 WiFi 配置，设备自动进入 **SoftAP 配网模式**
2. 手机连接 WiFi: `ESP-AP-XXXX`（无密码）
3. 浏览器访问: `192.168.4.1`
4. 输入 WiFi 名称和密码
5. 保存后设备自动连接并启动 AI Agent

### 按键功能

| 按键 | 操作 | 功能 |
|------|------|------|
| **PTT** | 按住 | 开始录音（对讲模式） |
| **PTT** | 松开 | 停止录音，发送音频 |
| **SWITCH** | 短按 | 切换 AI Agent（小智 ↔ 豆包） |
| **BOOT** | 按住 2 秒 | 清除 WiFi 配置，重启进入配网模式 |

### 串口监控

```bash
# Windows
python -m serial.tools.miniterm COM4 115200

# Linux/Mac
python -m serial.tools.miniterm /dev/ttyUSB0 115200
```

---

## 分区表

```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000    # WiFi 配置、Agent 选择
phy_init, data, phy,     0xf000,   0x1000    # RF 校准数据
factory,  app,  factory, 0x10000,  0x800000  # 固件 (8MB)
storage,  data, spiffs,  0x810000, 0x7F0000  # 用户数据
```

**注意**: 仅刷写 `esp32s3_chatbot.bin` 到 `0x10000` 不会覆盖 NVS 分区，WiFi 配置会保留。

---

## 故障排除

### 问题：无法连接 WiFi

- 确认 WiFi 名称和密码正确
- 确认路由器 2.4GHz 频段已开启
- 尝试按住 BOOT 键 2 秒重新配网

### 问题：按键无响应

- 检查接线是否牢固
- 确认按键另一端接 GND
- 用万用表测量按键通断

### 问题：串口无输出

- 确认 USB 线支持数据传输（非纯充电线）
- 检查 COM 端口号是否正确
- 尝试按 RST 键重启设备

### 问题：设备频繁重启

- 检查供电是否稳定（建议使用 USB 供电）
- 查看串口日志中的 `heap_int` 值，若低于 1000 说明内存不足

---

## 技术规格

- **固件版本**: v22e (2026-11-03)
- **ESP-IDF**: v5.3.1
- **框架**: esp-brookesia v0.8.1
- **AI Agent**: 小智 (MQTT) + 豆包 (OpenAI Realtime WebSocket)
- **内存优化**: button_task 栈分配至 PSRAM，避免内部 RAM 碎片化

---

## 参考资料

- [ESP32-S3 技术参考手册](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_cn.pdf)
- [esp-brookesia 框架文档](https://github.com/espressif/esp-brookesia)
- [小智 AI 协议](https://xiaozhi.me/)
- [豆包 OpenAI Realtime API](https://www.volcengine.com/docs/82379/1399008)
