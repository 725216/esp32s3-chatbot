# 屏幕功能启用指南（预留，默认关闭）

固件已内置完整的屏幕模块源码（`main/modules/display/`：LVGL UI + 表情 emote + 手势 +
设置页），但当前无屏硬件默认**不编译**该模块。以后接上屏幕时，按下面步骤打开即可。

## 1. 打开编译开关

```bash
idf.py menuconfig
# Display (屏幕) Configuration -> Enable display (LCD + LVGL UI) support  [y]
```

等价于在 `sdkconfig` 中加入：

```
CONFIG_APP_ENABLE_DISPLAY=y
```

打开后 `main/CMakeLists.txt` 会自动把 `modules/display/` 全部源码编入固件，
`main.cpp` 会在服务就绪后调用 `Display::get_instance().start()`（启动失败不致命，
自动退回无屏模式继续音频对话）。

## 2. 添加屏幕组件依赖

仓库里已备好一份含注释依赖块的模板 `main/idf_component.yml.display_bak`，
把其中 `# espressif/brookesia_service_display:` 往下的几行取消注释、追加到
`main/idf_component.yml` 的 `dependencies:` 下即可（或手动复制下面的内容）。

在 `main/idf_component.yml` 的 `dependencies:` 下追加：

```yaml
  # brookesia 屏幕/视频/表情服务（vendor 副本已含源码，用相对路径引用）
  espressif/brookesia_service_display:
    version: "0.8.*"
    override_path: ../vendor/esp-brookesia/service/media/brookesia_service_display
  espressif/brookesia_service_video:
    version: "0.8.*"
    override_path: ../vendor/esp-brookesia/service/media/brookesia_service_video
  espressif/brookesia_expression_emote:
    version: "0.8.*"
    override_path: ../vendor/esp-brookesia/service/expression/brookesia_expression_emote

  # display.cpp 还依赖 LVGL 与适配层（需联网从 registry 拉取）
  lvgl/lvgl: "^9"
  espressif/esp_lvgl_adapter: "<1"
```

> **版本注意**
> - `esp_lvgl_adapter` 最新版要求 **ESP-IDF >= 5.5**；本项目锁在 IDF 5.3.1，
>   需选择兼容 5.3 的旧版本，或整体升级 IDF。
> - `brookesia/gui_lvgl.hpp`（`gui::lvgl::DisplaySource`）在 vendor 精简副本中未包含，
>   若 registry 无 `espressif/brookesia_gui` 组件，需从 esp-brookesia 仓库 `gui/`
>   目录 vendor 一份并用 `override_path` 引用。

## 3. 使能 HAL 显示设备与面板驱动

继续在 `idf.py menuconfig` 中：

```
Brookesia HAL Adaptor Configuration
  -> Enable Display Device                      [y]
  -> LCD Panel 驱动（按你的屏幕选型，如 SPI ST7789 / QSPI GC9A01）
  -> Touch 驱动（如用了触摸）
```

并在板级配置 `boards/custom_s3/board_devices.yaml` / `board_peripherals.yaml`
中补上 LCD 面板外设（参考 esp-brookesia 官方 chatbot 示例板配置）。

## 4. 接线（以常见 1.69" ST7789 SPI 屏为例）

| 屏幕引脚 | ESP32-S3 GPIO | 说明 |
|---------|--------------|------|
| SCL (CLK) | GPIO12 | SPI 时钟 |
| SDA (MOSI)| GPIO11 | SPI 数据 |
| DC | GPIO10 | 数据/命令切换 |
| CS | GPIO15 | 片选 |
| RST | GPIO16 | 复位 |
| BLK | GPIO17 | 背光（可串 100Ω 接 3.3V） |
| VCC | 3.3V | 勿接 5V |
| GND | GND | |

> **已占用引脚（不可用于屏幕）**：
> - GPIO 4/5/6：INMP441 麦克风（WS/BCLK/DOUT）
> - GPIO 7/8/9：MAX98357A 功放（WS/BCLK/DIN）
> - GPIO 0：BOOT 键；GPIO 21：切换键；GPIO 38：PTT 键
> - GPIO 19/20：USB D-/D+；GPIO 35/36/37：Octal PSRAM 专用
>
> 剩余可用：GPIO 10~18、41~42、45~48（若接了屏，建议把 PTT/切换键移到 47/48 等，
> 把 10~17 整段留给屏幕 SPI）。若屏幕带触摸（I2C），SCL/SDA 可用 GPIO13/14。

## 5. 表情资源分区

`partitions.csv` 已预留 `storage` 分区（0x810000 起），emote 表情资源可打包写入：

```bash
idf.py spiffscreate flash_spiffs   # 或按 brookesia emote 资产工具生成
```

## 6. 重新构建

```bash
idf.py build
idf.py -p COM4 flash
```

云编译（GitHub Actions）同样支持：推送打开 `CONFIG_APP_ENABLE_DISPLAY=y`
（写入 sdkconfig.defaults）后的代码即可，CI 容器会联网自动拉取 LVGL 等 registry 依赖。
