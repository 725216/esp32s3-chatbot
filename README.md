# ESP32-S3 双AI语音对话固件

基于 ESP32-S3-N16R8 的双AI语音对话固件，支持**小智**和**豆包**两个AI Agent无缝切换。

## 特性

- 🎙️ **按键对讲** (PTT): 按住说话，松开停止
- 🔄 **Agent 切换**: 一键切换小智 ↔ 豆包 OpenAI
- 📶 **WiFi 配网**: SoftAP 配网模式，手机浏览器配置
- 🔐 **安全配置**: API Key 通过配网页输入，不编译进固件
- 💾 **配置持久化**: NVS 存储 WiFi 和 Agent 配置
- 🧠 **内存优化**: button_task 栈分配至 PSRAM，避免内部 RAM 碎片化
- 📺 **屏幕预留**: LVGL UI/表情/手势模块已内置（默认关闭），以后接屏即可启用
- ☁️ **云编译**: GitHub Actions 自动构建，Release 自动发布

## 硬件要求

| 组件 | 规格 |
|------|------|
| 主控 | ESP32-S3-N16R8 (16MB Flash, 8MB PSRAM Octal) |
| 麦克风 | INMP441 I2S 数字麦克风 |
| 功放 | MAX98357A I2S 功放 + 无源喇叭 (4Ω/8Ω) |
| 按键 | 2× 轻触按键 (PTT + SWITCH) |
| 供电 | USB-C 5V（建议 ≥500mA，带喇叭外放时峰值电流较大） |
| 屏幕（可选） | SPI/QSPI LCD，如 ST7789 1.69"（见 docs/DISPLAY.md） |

## 完整接线方法

### 按键

| 功能 | GPIO | 接线 | 操作 |
|------|------|------|------|
| PTT 对话键 | **GPIO38** | 按键一端接 GPIO38，另一端接 **GND** | 按住说话，松开停止 |
| SWITCH 切换键 | **GPIO21** | 按键一端接 GPIO21，另一端接 **GND** | 短按切换小智 ↔ 豆包 |
| BOOT 重配网 | **GPIO0** | 板载按键，无需接线 | 开机按住 2 秒：清 WiFi 重配网（保留 Token） |

> 固件已启用内部上拉，无需外接电阻。按下=低电平，松开=高电平。

### INMP441 I2S 麦克风（录音）

| INMP441 | ESP32-S3 | 说明 |
|---------|----------|------|
| VDD | 3.3V | 供电 |
| GND | GND | 地 |
| WS | **GPIO4** | 帧时钟 |
| SCK | **GPIO5** | 位时钟 |
| SD | **GPIO6** | 数据输出 |
| L/R | GND | 左声道（固件单声道左槽） |
| SEL | GND | SCK×64 模式 |

### MAX98357A I2S 功放（播放）

| MAX98357A | ESP32-S3 | 说明 |
|-----------|----------|------|
| Vin | 5V 或 3.3V | 5V 音量更大 |
| GND | GND | 地 |
| BCLK | **GPIO8** | 位时钟 |
| L/R | **GPIO7** | 帧时钟 |
| DIN | **GPIO9** | 数据输入 |
| SD_MODE | 3.3V | **必须拉高**，悬空不工作 |
| Vo+/Vo- | 喇叭两极 | 无源喇叭，不分正负 |

### 引脚占用总览

| GPIO | 用途 | GPIO | 用途 |
|------|------|------|------|
| 0 | BOOT 键 | 21 | SWITCH 键 |
| 4/5/6 | 麦克风 WS/SCK/SD | 38 | PTT 键 |
| 7/8/9 | 功放 L/R/BCLK/DIN | 45/46 | strapping，尽量空闲 |
| 19/20 | USB D-/D+（勿动） | 48 | 板载 RGB LED |
| 35/36/37 | Octal PSRAM 专用（勿接外设） | 10~18, 41~42, 47 | **空闲，可留给屏幕扩展** |
| 1 | 不可用（板载电路拉低） | | |

接线示意图见 [docs/WIRING.md](docs/WIRING.md)。

## 快速开始

### 1. 刷写固件

**方式一：下载预编译固件**

从 [Releases](https://github.com/YOUR_REPO/esp32s3_chatbot/releases) 下载最新固件：

```bash
# 完整刷写（首次）
esptool.py --port COM4 --baud 921600 write_flash \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 esp32s3_chatbot.bin

# 仅更新 App（保留配置）
esptool.py --port COM4 --baud 921600 write_flash 0x10000 esp32s3_chatbot.bin
```

**方式二：本地编译**

```bash
# 安装 ESP-IDF v5.3.1
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/

# 克隆仓库
git clone https://github.com/YOUR_REPO/esp32s3_chatbot.git
cd esp32s3_chatbot

# 编译
idf.py build

# 刷写
idf.py -p COM4 flash
```

### 2. 首次配置（重要）

**固件不包含 API Key**，首次启动后需通过配网页输入：

1. 上电后设备进入 SoftAP 配网模式
2. 手机连接 WiFi: `ESP-AP-XXXX`（无密码）
3. 浏览器访问: `192.168.4.1`
4. 在"豆包 Agent 配置"区域输入：
   - **APP ID**: 火山方舟控制台获取
   - **Access Token**: 火山方舟控制台获取（sk-xxx）
   - **Model**: 如 `1.2.6.1` (Seed-1.2.6.1)
   - **Voice**: 如 `zh_female_vv_uranus_bigtts`
5. 点击"保存 Agent 配置"
6. 输入 WiFi 名称和密码
7. 点击"Connect"连接

### 3. 使用

| 按键 | 操作 | 功能 |
|------|------|------|
| **PTT** | 按住 | 开始录音 |
| **PTT** | 松开 | 停止录音，发送音频 |
| **SWITCH** | 短按 | 切换 AI Agent |
| **BOOT** | 按住 2 秒 | 清除配置，重新配网 |

## 配网页说明

访问 `192.168.4.1` 后会看到：

```
┌─────────────────────────────────┐
│  Wi-Fi Provisioning             │
├─────────────────────────────────┤
│  [WiFi 列表]                    │
│                                 │
│  豆包 Agent 配置                │
│  ┌───────────────────────────┐ │
│  │ APP ID                    │ │
│  │ Access Token (sk-xxx)     │ │
│  │ Model                     │ │
│  │ Voice                     │ │
│  └───────────────────────────┘ │
│  [保存 Agent 配置]              │
│                                 │
│  SSID: [________]              │
│  Password: [________]          │
│  [Connect]                      │
└─────────────────────────────────┘
```

## 云编译

本项目使用 GitHub Actions 自动构建固件。

### 触发条件

- 推送到 `main` / `master` 分支
- 创建 `v*` 标签（自动发布 Release）
- 手动触发 (`workflow_dispatch`)

### 安全说明

- ✅ **API Key 不编译进固件** — 由配网页输入后存储到 NVS
- ✅ **WiFi 密码不编译进固件** — 由配网页输入后存储到 NVS
- ✅ **sdkconfig 已忽略** — 在 `.gitignore` 中，不会提交到仓库
- ✅ **构建使用占位符** — `sk-your-api-key-here`，用户首次启动后配置

### 构建产物

- `esp32s3_chatbot.bin` - 应用固件
- `bootloader.bin` - 引导程序
- `partition-table.bin` - 分区表
- `FLASH_README.md` - 刷写说明

### 发布 Release

```bash
# 创建标签
git tag v1.0.0
git push origin v1.0.0

# GitHub Actions 自动构建并发布 Release
```

## 项目结构

```
esp32s3_chatbot/
├── .github/workflows/
│   └── build.yml          # GitHub Actions 云编译工作流
├── docs/
│   ├── WIRING.md          # 完整接线文档（按键/麦克风/功放/引脚占用）
│   └── DISPLAY.md         # 屏幕功能启用指南（预留）
├── main/
│   ├── main.cpp           # 主程序（Agent 切换逻辑）
│   └── modules/
│       ├── wifi_provisioning.cpp  # WiFi 配网模块
│       └── display/       # 屏幕模块（LVGL UI/表情/手势，默认不编译）
├── vendor/
│   └── esp-brookesia/     # AI Agent 框架
├── partitions.csv         # 分区表
├── sdkconfig.defaults     # 默认配置（不含敏感信息）
└── README.md              # 本文件
```

## 屏幕功能（预留，默认关闭）

固件已内置完整屏幕模块源码（`main/modules/display/`：LVGL UI + 表情 emote + 手势 +
设置页），当前无屏硬件默认**不编译**，不影响固件体积和内存。以后接上屏幕只需：

1. `idf.py menuconfig` → **Display (屏幕) Configuration** → 打开
   `Enable display (LCD + LVGL UI) support`（即 `CONFIG_APP_ENABLE_DISPLAY=y`）
2. 按 [docs/DISPLAY.md](docs/DISPLAY.md) 添加屏幕组件依赖（display/video/emote + LVGL）
   并使能 HAL 显示设备与 LCD 面板驱动
3. 屏幕接线（ST7789 SPI 示例：DC=10, MOSI=11, SCLK=12, CS=15, RST=16, BLK=17，
   这些引脚当前均空闲）；表情资源分区 storage 已预留

`main.cpp` 在服务就绪后自动调用 `Display::start()`；启动失败不致命，
自动退回无屏模式继续音频对话。

## 技术细节

### 内存优化

ESP32-S3 内部 RAM 有限（约 512KB），多任务并发时容易碎片化。本固件将 button_task 栈分配至 PSRAM：

```cpp
// 从 PSRAM 分配 6KB 栈
static StackType_t *s_button_task_stack = nullptr;
static StaticTask_t s_button_task_tcb;

s_button_task_stack = (StackType_t *)heap_caps_aligned_alloc(
    16, 6 * 1024 * sizeof(StackType_t),
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

TaskHandle_t handle = xTaskCreateStaticPinnedToCore(
    button_task, "button_task",
    6 * 1024, nullptr, 5,
    s_button_task_stack, &s_button_task_tcb, 0);
```

### Agent 切换

切换时增加 500ms 延迟，让 WiFi 缓冲区释放完毕：

```cpp
if (strcmp(new_agent, AGENT_OPENAI) == 0) {
    ESP_LOGI(LOG_TAG, "OpenAI switch: waiting 500ms for WiFi buffers to settle");
    vTaskDelay(pdMS_TO_TICKS(500));
}
```

### 配置存储

- **NVS 分区** (`0x9000-0xF000`): 存储 WiFi 配置和 Agent 配置
- **仅更新 App 时保留**: `esptool.py write_flash 0x10000 esp32s3_chatbot.bin`
- **清除配置**: 按住 BOOT 键 2 秒

### 分区表

```
nvs,      data, nvs,     0x9000,   0x6000    # WiFi 配置、Agent 配置
phy_init, data, phy,     0xf000,   0x1000    # RF 校准数据
factory,  app,  factory, 0x10000,  0x800000  # 固件 (8MB)
storage,  data, spiffs,  0x810000, 0x7F0000  # 用户数据
```

## 故障排除

| 问题 | 解决方案 |
|------|---------|
| 豆包无法连接 | 检查 Access Token 是否正确（sk-xxx） |
| 无法连接 WiFi | 确认 2.4GHz 频段已开启；按住 BOOT 2 秒重新配网 |
| 按键无响应 | 检查接线；确认按键另一端接 GND |
| 设备频繁重启 | 检查供电；查看串口日志 `heap_int` 值 |
| 切换后无声音 | 等待 1-2 秒；检查 WiFi 连接状态 |
| 配网页打不开 | 确认连接了 `ESP-AP-XXXX` WiFi |

## 参考资料

- [ESP32-S3 技术参考手册](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_cn.pdf)
- [esp-brookesia 框架](https://github.com/espressif/esp-brookesia)
- [小智 AI](https://xiaozhi.me/)
- [豆包 OpenAI Realtime API](https://www.volcengine.com/docs/82379/1399008)

## 许可证

MIT License

## 贡献

本固件Ai生成，有问题找Ai
