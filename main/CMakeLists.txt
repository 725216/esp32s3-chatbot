# ESP32-S3 无屏双 Agent（小智 + 豆包/OpenAI）工程
# 显式列出源文件：profiler/ai_agents 模块不参与编译（无屏精简）
set(SRCS
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/modules/general_services.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/modules/wifi_provisioning.cpp"
)

# 屏幕功能（LVGL UI + 表情 emote + 手势）：默认关闭，接上屏后在 menuconfig
# 打开 Display (屏幕) Configuration -> APP_ENABLE_DISPLAY 即可编译进固件。
# 完整启用步骤（加依赖/使能 HAL 显示设备/屏幕接线）见 docs/DISPLAY.md。
if(CONFIG_APP_ENABLE_DISPLAY)
    list(APPEND SRCS
        "${CMAKE_CURRENT_LIST_DIR}/modules/display/display.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/modules/display/screens/emote.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/modules/display/screens/settings.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/modules/display/squareline_ui/ui.c"
        "${CMAKE_CURRENT_LIST_DIR}/modules/display/squareline_ui/ui_comp_hook.c"
        "${CMAKE_CURRENT_LIST_DIR}/modules/display/squareline_ui/ui_helpers.c"
        "${CMAKE_CURRENT_LIST_DIR}/modules/display/squareline_ui/screens/ui_ScreenSettings.c"
    )
endif()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES nvs_flash
)

# esp_peer 预编译库（esp-webrtc）引用了 libnosys 的 telldir/seekdir/rewinddir，
# 与 IDF vfs 组件的同名 strong 符号冲突；允许重复定义（取先链接的 vfs 实现）。
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--allow-multiple-definition")

# 无屏工程：不生成 emote assets、不使用 littlefs
