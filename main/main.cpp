/*
 * ESP32-S3 无屏双 Agent（小智 + 豆包/OpenAI）语音对话固件
 * - GPIO38: PTT 对话键（按住说话 / 松开停止）
 * - GPIO21: Agent 切换键（小智 <-> 豆包 OpenAI）
 * - WiFi SoftAP 配网（无已存 AP 时自动进入配网模式；按住 BOOT(GPIO0) 通电 2 秒重配网）
 */
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "private/utils.hpp"
#include "brookesia/lib_utils.hpp"
#include "brookesia/service_helper.hpp"
#include "brookesia/service_manager.hpp"
#include "modules/wifi_provisioning.hpp"
#include "modules/general_services.hpp"
/* 屏幕功能（默认关闭）：menuconfig 打开 APP_ENABLE_DISPLAY 后编译 display 模块，
 * 启用步骤见 docs/DISPLAY.md */
#ifdef CONFIG_APP_ENABLE_DISPLAY
#include "modules/display/display.hpp"
#endif

using namespace esp_brookesia;

using AgentHelper = service::helper::AgentManager;
using OpenaiHelper = service::helper::Openai;
using XiaoZhiHelper = service::helper::XiaoZhi;
using WifiHelper = service::helper::Wifi;

#define LOG_TAG "app_main"

/* 按键 GPIO（用户板左侧引脚）
 * LOCAL FIX (2026-10-27): GPIO1/GPIO47 被板载电路拉低（恒低）不可用；
 * 换左侧闲置纯 GPIO：GPIO2（PTT 对话）/ GPIO21（切换）。
 * LOCAL FIX (2026-10-31): GPIO2 PTT 物理链路失败（KEY diag 恒 PTT=1，杜邦线短接亦不变 0），
 * 按用户指令换 GPIO38 作为 PTT 对话键（避开 35/36/37=Octal PSRAM、1=板载拉低）。
 * 2026-11-02: GPIO38 短接 GND 亦读不到 0（manual24）。加 BOOT 电平入 KEY diag 验证机制：
 *            若按板载 BOOT(GPIO0) 电平变化则机制正常、是引脚认错/排针虚焊；否则为固件读取问题。
 * 避开：0=BOOT(重配网)、19/20=USB、45=strapping、48=LED、4-9=I2S。 */
#define KEY_PTT_GPIO      GPIO_NUM_38
#define KEY_SWITCH_GPIO   GPIO_NUM_21
/* 重配网触发键：板载 BOOT 键（GPIO0）。按住 BOOT 通电 >=2 秒 -> 清 NVS + 重启进入 SoftAP 配网 */
#define KEY_BOOT_GPIO     GPIO_NUM_0

/* setup_task 完成标志：按键任务等待服务就绪后再轮询，避免服务未启动时误操作 */
static volatile bool s_setup_done = false;

/* 上电重配网：开机 3 秒内按住 BOOT 键（GPIO0）>= 2 秒 -> 清 WiFi 配置 + 重启，进入 SoftAP 重新配网
 * LOCAL FIX (2026-10-27): 原实现检测 GPIO1/GPIO47 导致误判（板载电路拉低）重启循环，
 * 改为只检测 BOOT 键，与对话键/切换键彻底分离。
 * v22e (2026-11-03): 只擦除 WiFi 配置，保留 Agent Token/API Key（agent_cfg 命名空间），
 * 用户重新配网时不需要重新输入 Token。 */
static bool check_boot_reprovision()
{
    gpio_config_t io_conf{};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pin_bit_mask = (1ULL << KEY_BOOT_GPIO);
    gpio_config(&io_conf);

    /* 诊断：打印启动时各按键电平（帮助排查接线/板载下拉问题） */
    ESP_LOGI(LOG_TAG, "Boot key check: BOOT(GPIO0)=%d PTT(GPIO38)=%d SWITCH(GPIO21)=%d",
             gpio_get_level(KEY_BOOT_GPIO), gpio_get_level(KEY_PTT_GPIO), gpio_get_level(KEY_SWITCH_GPIO));

    int boot_hold = 0;
    for (int i = 0; i < 30; i++) {            /* 3 秒窗口 */
        if (gpio_get_level(KEY_BOOT_GPIO) == 0) {   /* BOOT 按下=低电平 */
            boot_hold++;
            if (boot_hold >= 20) {            /* 连续按住 >= 2 秒 */
                return true;
            }
        } else {
            boot_hold = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}

static void do_reprovision()
{
    /* v22e (2026-11-03): 只擦除 WiFi 配置，保留 Agent Token/API Key
     * - WiFi 配置存储在 "wifi" 命名空间
     * - Agent 配置存储在 "agent_cfg" 命名空间（保留）
     * 这样用户重新配网时不需要重新输入 Token */
    ESP_LOGW(LOG_TAG, "Boot key held: erasing WiFi config only, preserving Agent token...");
    
    nvs_handle_t handle;
    if (nvs_open("wifi", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(LOG_TAG, "WiFi config erased");
    } else {
        ESP_LOGW(LOG_TAG, "WiFi namespace not found, nothing to erase");
    }
    
    /* 同时清除 "LastAp" 存储（esp-brookesia 框架使用） */
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, "LastAp");
        nvs_commit(handle);
        nvs_close(handle);
    }
    
    ESP_LOGI(LOG_TAG, "Restarting for re-provisioning (Agent token preserved)...");
    esp_restart();
}

/* Agent 名称 */
static constexpr const char *AGENT_XIAOZHI = "XiaoZhi";
static constexpr const char *AGENT_OPENAI  = "OpenAI";

static const char *current_target_agent = AGENT_XIAOZHI;

/* 服务绑定与事件连接（必须保持存活） */
static std::vector<service::ServiceBinding> s_service_bindings;
static std::vector<service::EventRegistry::SignalConnection> s_service_connections;

static bool s_wifi_connected = false;
static std::shared_ptr<lib_utils::TaskScheduler> s_task_scheduler;

/* 日志降噪 */
static void setup_log_level()
{
    esp_log_level_set("ESP_GMF_TASK", ESP_LOG_WARN);
    esp_log_level_set("ESP_GMF_FILE", ESP_LOG_WARN);
    esp_log_level_set("ESP_GMF_PORT", ESP_LOG_WARN);
    esp_log_level_set("ESP_GMF_POOL", ESP_LOG_WARN);
    esp_log_level_set("ESP_ES_PARSER", ESP_LOG_WARN);
    esp_log_level_set("GMF_AFE", ESP_LOG_WARN);
    esp_log_level_set("ESP_GMF_AENC", ESP_LOG_WARN);
    esp_log_level_set("AFE_MANAGER", ESP_LOG_WARN);
    esp_log_level_set("AUD_RENDER", ESP_LOG_WARN);
    esp_log_level_set("AUDIO_PROCESSOR", ESP_LOG_WARN);
    esp_log_level_set("AUD_SIMP_PLAYER", ESP_LOG_WARN);
    esp_log_level_set("AUD_SDEC", ESP_LOG_WARN);
    esp_log_level_set("AFE_CONFIG", ESP_LOG_WARN);
    esp_log_level_set("NEW_DATA_BUS", ESP_LOG_WARN);
    esp_log_level_set("ESP_XIAOZHI_MQTT", ESP_LOG_WARN);
    esp_log_level_set("ESP_XIAOZHI_CHAT", ESP_LOG_WARN);
    esp_log_level_set("AFE", ESP_LOG_ERROR);
}

/* 启动 / 停止当前 target agent */
static void start_agent()
{
    ESP_LOGI(LOG_TAG, "Starting agent: %s", current_target_agent);
    AgentHelper::call_function_async(
        AgentHelper::FunctionId::TriggerGeneralAction,
        BROOKESIA_DESCRIBE_TO_STR(AgentHelper::GeneralAction::Activate)
    );
    AgentHelper::call_function_async(
        AgentHelper::FunctionId::TriggerGeneralAction,
        BROOKESIA_DESCRIBE_TO_STR(AgentHelper::GeneralAction::Start)
    );
}

static void stop_agent()
{
    ESP_LOGI(LOG_TAG, "Stopping agent");
    AgentHelper::call_function_async(
        AgentHelper::FunctionId::TriggerGeneralAction,
        BROOKESIA_DESCRIBE_TO_STR(AgentHelper::GeneralAction::Stop)
    );
}

/* WiFi 事件处理：Connected -> 启动 agent，Disconnected -> 停止 agent */
static void process_wifi_events()
{
    auto general_event_happened_slot =
    [](const std::string &event_name, const std::string &wifi_event, bool is_unexpected) {
        (void)event_name;
        (void)is_unexpected;
        WifiHelper::GeneralEvent wifi_event_enum;
        if (!BROOKESIA_DESCRIBE_STR_TO_ENUM(wifi_event, wifi_event_enum)) {
            ESP_LOGW(LOG_TAG, "Failed to convert wifi event: %s", wifi_event.c_str());
            return;
        }
        switch (wifi_event_enum) {
        case WifiHelper::GeneralEvent::Connected: {
            ESP_LOGI(LOG_TAG, "WiFi connected");
            s_wifi_connected = true;
            if (s_task_scheduler) {
                s_task_scheduler->post([]() { start_agent(); });
            }
            break;
        }
        case WifiHelper::GeneralEvent::Disconnected: {
            ESP_LOGW(LOG_TAG, "WiFi disconnected");
            s_wifi_connected = false;
            if (s_task_scheduler) {
                s_task_scheduler->post([]() { stop_agent(); });
            }
            break;
        }
        default:
            break;
        }
    };
    auto conn = WifiHelper::subscribe_event(WifiHelper::EventId::GeneralEventHappened, general_event_happened_slot);
    if (conn.connected()) {
        s_service_connections.push_back(std::move(conn));
    } else {
        ESP_LOGE(LOG_TAG, "Failed to subscribe to WiFi general event happened event");
    }
}

/* 订阅小智激活码事件：日志打印（无屏设备，用手机小智 App 输入绑定） */
static void subscribe_xiaozhi_activation_code()
{
    auto slot = [](const std::string &event_name, const std::string &code) {
        (void)event_name;
        ESP_LOGW(LOG_TAG, "XiaoZhi activation code: [%s]  (enter it in Xiaozhi App to bind this device)", code.c_str());
    };
    auto conn = XiaoZhiHelper::subscribe_event(XiaoZhiHelper::EventId::ActivationCodeReceived, slot);
    if (conn.connected()) {
        s_service_connections.push_back(std::move(conn));
    } else {
        ESP_LOGE(LOG_TAG, "Failed to subscribe to XiaoZhi activation code event");
    }
}

/* Agent 切换：小智 <-> 豆包 OpenAI
 * LOCAL FIX (2026-10-30): WiFi 未连接时（SoftAP 配网/重连中）禁止立即启动新 agent，
 * 否则 agent(编码器+WS) 与 SoftAP(DHCP+HTTP+DNS) 并发耗尽 internal RAM -> lwip OOM 崩溃。
 * 目标先保存，WiFi 连上后由 Connected 事件自动 start_agent()。 */
static void switch_agent()
{
    const char *new_agent = (strcmp(current_target_agent, AGENT_XIAOZHI) == 0) ? AGENT_OPENAI : AGENT_XIAOZHI;
    ESP_LOGI(LOG_TAG, "Switching agent: %s -> %s", current_target_agent, new_agent);

    stop_agent();

    auto set_target_result = AgentHelper::call_function_sync(
                                 AgentHelper::FunctionId::SetTargetAgent, new_agent
                             );
    if (!set_target_result) {
        ESP_LOGE(LOG_TAG, "Failed to set target agent: %s", set_target_result.error().c_str());
        return;
    }
    current_target_agent = new_agent;

    if (!s_wifi_connected) {
        ESP_LOGW(LOG_TAG, "WiFi not connected, agent will start after reconnect");
        return;
    }

    /* v22d (2026-11-03): 切换到 OpenAI 前延迟 500ms——快速连续切换时 WiFi 缓冲区
     * 可能未释放完毕，导致 wifi:mem fail 和 WebSocket 连接失败。延迟让 WiFi 栈稳定。 */
    if (strcmp(new_agent, AGENT_OPENAI) == 0) {
        ESP_LOGI(LOG_TAG, "OpenAI switch: waiting 500ms for WiFi buffers to settle");
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!s_wifi_connected) {
            ESP_LOGW(LOG_TAG, "WiFi lost during OpenAI settle delay, aborting start");
            return;
        }
    }

    /* 切换后激活并启动新 agent：延迟到 scheduler 执行，执行时再校验 WiFi 状态，
     * 防止切换刚发出而 WiFi 恰好断开时仍启动 agent（与 SoftAP 配网并发 OOM）。 */
    if (s_task_scheduler) {
        s_task_scheduler->post([]() {
            if (!s_wifi_connected) {
                ESP_LOGW(LOG_TAG, "WiFi lost before agent start, aborting start");
                return;
            }
            AgentHelper::call_function_async(
                AgentHelper::FunctionId::TriggerGeneralAction,
                BROOKESIA_DESCRIBE_TO_STR(AgentHelper::GeneralAction::Activate)
            );
            AgentHelper::call_function_async(
                AgentHelper::FunctionId::TriggerGeneralAction,
                BROOKESIA_DESCRIBE_TO_STR(AgentHelper::GeneralAction::Start)
            );
        });
    }
}

/* 按键轮询任务：
 * - GPIO38 PTT：按下 -> ManualStartListening，松开 -> ManualStopListening
 * - GPIO21 切换：检测到下降沿 -> switch_agent()
 * 等待 setup_done 标志（服务全部就绪）后才开始轮询。
 */
static void button_task(void *arg)
{
    (void)arg;
    while (!s_setup_done) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelay(pdMS_TO_TICKS(100));   /* 再等服务稳定 100ms */

    gpio_config_t io_conf{};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pin_bit_mask = (1ULL << KEY_PTT_GPIO) | (1ULL << KEY_SWITCH_GPIO);
    gpio_config(&io_conf);

    /* LOCAL FIX (2026-10-27): prev 初始化为实际电平，避免引脚恒低（未接/板载下拉）时
     * 首轮轮询误判"按下"而误触 PTT/切换。只有电平从 1->0 的下降沿才触发。 */
    int prev_ptt = gpio_get_level(KEY_PTT_GPIO);
    int prev_switch = gpio_get_level(KEY_SWITCH_GPIO);
    const TickType_t debounce = pdMS_TO_TICKS(50);

    ESP_LOGI(LOG_TAG, "Button task started: PTT=GPIO%d, Switch=GPIO%d", KEY_PTT_GPIO, KEY_SWITCH_GPIO);

    /* 2026-11-02 用户要求恢复按键说话：AUTO 定时钩子已移除，
     * 说话由 PTT 按键控制（按下=开始听、松开=停止听）。 */
    while (1) {
        int ptt = gpio_get_level(KEY_PTT_GPIO);
        int sw = gpio_get_level(KEY_SWITCH_GPIO);

        /* PTT 按下（低电平） */
        if (prev_ptt == 1 && ptt == 0) {
            vTaskDelay(debounce);
            if (gpio_get_level(KEY_PTT_GPIO) == 0) {
                ESP_LOGI(LOG_TAG, "PTT pressed, start listening");
                AgentHelper::call_function_async(AgentHelper::FunctionId::ManualStartListening);
            }
        } else if (prev_ptt == 0 && ptt == 1) {
            vTaskDelay(debounce);
            if (gpio_get_level(KEY_PTT_GPIO) == 1) {
                ESP_LOGI(LOG_TAG, "PTT released, stop listening");
                AgentHelper::call_function_async(AgentHelper::FunctionId::ManualStopListening);
            }
        }
        prev_ptt = ptt;

        /* 切换键按下（低电平，单击触发一次） */
        if (prev_switch == 1 && sw == 0) {
            vTaskDelay(debounce);
            if (gpio_get_level(KEY_SWITCH_GPIO) == 0) {
                switch_agent();
            }
        }
        prev_switch = sw;

        /* LOCAL FIX (2026-10-30): 按键电平诊断——每 2s 打印一次，
         * 用于确认 PTT(GPIO38)/SWITCH(GPIO21) 空闲电平是否稳定（应为 1）。
         * 2026-11-01: 自动测试钩子已删除（链路实测通过）。 */
        {
            static uint32_t s_last_diag_ms = 0;
            uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            if (now_ms - s_last_diag_ms > 2000) {
                s_last_diag_ms = now_ms;
                ESP_LOGI(LOG_TAG, "KEY diag: PTT=%d SW=%d BOOT=%d heap_int=%d largest=%d",
                         gpio_get_level(KEY_PTT_GPIO), gpio_get_level(KEY_SWITCH_GPIO),
                         gpio_get_level(KEY_BOOT_GPIO),
                         (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                         (int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                /* 2026-11-02 GPIO 回读自测：引脚切输出拉低->读回->恢复输入上拉。
                 * 自证 GPIO 子系统/引脚硬件是否正常（不依赖外部按键操作）。
                 * 输出 0 读回 0 = 引脚正常；读回 1 = 引脚或 GPIO 子系统异常。 */
                /* v22 (2026-11-03): GPIO 自测已禁用——自测将 SWITCH( GPIO21) 设为 OUTPUT
                 * 拉低 10ms，与按键轮询竞争同一引脚，导致切换按键检测失败。
                 * 诊断使命已完成，正式固件不再需要。 */
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* 从 NVS 读取 Agent 配置（配网页写入）。返回 true 表示有可用配置 */
static bool load_agent_cfg_from_nvs(const char *key, char *out, size_t out_size)
{
    nvs_handle_t handle;
    if (nvs_open("agent_cfg", NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t len = out_size;
    esp_err_t ret = nvs_get_str(handle, key, out, &len);
    nvs_close(handle);
    return (ret == ESP_OK && len > 0);
}

/* 豆包 OpenAI Agent 信息：优先用配网页写入的 NVS 配置，否则用编译期占位 */
static boost::json::object get_agent_openai_info()
{
    char buf[256];
    std::string model   = CONFIG_EXAMPLE_AGENTS_OPENAI_MODEL;
    std::string api_key = CONFIG_EXAMPLE_AGENTS_OPENAI_API_KEY;
    std::string voice   = CONFIG_EXAMPLE_AGENTS_OPENAI_VOICE;
    std::string app_id  = CONFIG_EXAMPLE_AGENTS_OPENAI_APP_ID;

    if (load_agent_cfg_from_nvs("openai_api_key", buf, sizeof(buf))) {
        api_key = buf;
        ESP_LOGI(LOG_TAG, "OpenAI API key loaded from NVS");
    }
    if (load_agent_cfg_from_nvs("openai_model", buf, sizeof(buf))) {
        model = buf;
    }
    if (load_agent_cfg_from_nvs("openai_voice", buf, sizeof(buf))) {
        voice = buf;
    }
    if (load_agent_cfg_from_nvs("openai_app_id", buf, sizeof(buf))) {
        app_id = buf;
        ESP_LOGI(LOG_TAG, "OpenAI App ID loaded from NVS");
    }

    /* 火山方舟 Realtime 3.0 固定使用 Seed-1.2.6.1；Model/Voice 留空时补默认值，避免 SetAgentInfo 校验失败 */
    if (model.empty()) {
        model = "1.2.6.1";
    }
    if (voice.empty()) {
        voice = "zh_female_vv_uranus_bigtts";   /* Vivi 2.0 官方主推音色（2026-10-29；v17 试 jupiter 连接成功但不回话，2026-11-03 回退） */
    }

    OpenaiHelper::Info openai_info{
        .model = model.c_str(),
        .api_key = api_key.c_str(),
        .voice = voice.c_str(),
        .app_id = app_id.c_str(),
    };
    return BROOKESIA_DESCRIBE_TO_JSON(openai_info).as_object();
}

extern "C" void app_main(void)
{
    ESP_LOGI(LOG_TAG, "\n\n=== ESP32-S3 双 Agent 语音对话固件（小智 + 豆包）===\n");

    setup_log_level();

    /* 上电重配网：按住任意按键（>=2 秒）通电 -> 清 NVS 重启进入 SoftAP 配网 */
    if (check_boot_reprovision()) {
        do_reprovision();
    }

    /* 创建后端任务调度器 */
    s_task_scheduler = std::make_shared<lib_utils::TaskScheduler>();
    auto start_result = s_task_scheduler->start({
        .worker_configs = {
            {
                .name = "BackendWorker1",
                .core_id = 0,
                .priority = 1,
                .stack_size = 10 * 1024,
            },
            {
                .name = "BackendWorker2",
                .core_id = 1,
                .priority = 1,
                .stack_size = 10 * 1024,
            },
        }
    });
    BROOKESIA_CHECK_FALSE_EXIT(start_result, "Failed to start task scheduler");

    auto setup_task = []() {
        ESP_LOGI(
            LOG_TAG, "Heap: internal8=%luB dram=%luB psram=%luB dma=%luB",
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DMA)
        );
        /* 初始化基础服务（audio/device/wifi 等） */
        GeneralServices::get_instance().init(s_task_scheduler);
        GeneralServices::get_instance().init_audio();
        GeneralServices::get_instance().start_device();

        /* 启动 ServiceManager */
        auto &service_manager = service::ServiceManager::get_instance();
        BROOKESIA_CHECK_FALSE_EXIT(service_manager.start(), "Failed to start ServiceManager");

        /* 绑定 AgentManager */
        auto agent_binding = service_manager.bind(AgentHelper::get_name().data());
        BROOKESIA_CHECK_FALSE_EXIT(agent_binding.is_valid(), "Failed to bind Agent manager service");
        s_service_bindings.push_back(std::move(agent_binding));

        /* 注册豆包（OpenAI Realtime 兼容）Agent 信息 */
        ESP_LOGI(LOG_TAG, "Setting OpenAI agent info...");
        auto openai_result = AgentHelper::call_function_sync(
                                 AgentHelper::FunctionId::SetAgentInfo,
                                 OpenaiHelper::get_name().data(), get_agent_openai_info()
                             );
        if (!openai_result) {
            ESP_LOGE(LOG_TAG, "Failed to set OpenAI agent info: %s", openai_result.error().c_str());
        } else {
            ESP_LOGI(LOG_TAG, "Set OpenAI agent info successfully");
        }

        /* 小智：0.8.2 无需 SetAgentInfo（激活码流程），直接可切换 */

        /* 设置 Manual 对话模式（PTT 按住说话） */
        auto set_mode_result = AgentHelper::call_function_sync(
                                   AgentHelper::FunctionId::SetChatMode,
                                   BROOKESIA_DESCRIBE_TO_STR(AgentHelper::ChatMode::Manual)
                               );
        if (!set_mode_result) {
            ESP_LOGE(LOG_TAG, "Failed to set chat mode: %s", set_mode_result.error().c_str());
        } else {
            ESP_LOGI(LOG_TAG, "Set chat mode to Manual (PTT)");
        }

        /* 2026-11-03 用户要求：通电默认小智；按切换键(GPIO21)在小智 <-> 豆包之间切换 */
        auto set_target_result = AgentHelper::call_function_sync(
                                     AgentHelper::FunctionId::SetTargetAgent, AGENT_XIAOZHI
                                 );
        if (!set_target_result) {
            ESP_LOGE(LOG_TAG, "Failed to set target agent: %s", set_target_result.error().c_str());
        }
        current_target_agent = AGENT_XIAOZHI;

        /* 订阅 WiFi 事件 */
        process_wifi_events();

        /* 订阅小智激活码事件（打印到串口日志） */
        subscribe_xiaozhi_activation_code();

        /* 启动 WiFi 配网模块（无已存 AP -> SoftAP 配网） */
        ESP_LOGI(LOG_TAG, "Heap before WiFi init: internal8=%luB dram=%luB psram=%luB dma=%luB dma_largest=%luB dma_int_largest=%luB dma_int8=%luB",
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DMA),
                 (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
                 (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        WifiProvisioning::get_instance().init({
            .task_scheduler = s_task_scheduler,
        });
        WifiProvisioning::get_instance().start();

#ifdef CONFIG_APP_ENABLE_DISPLAY
        /* 屏幕启动（LVGL UI + 表情 + 手势）：需要 HAL 显示设备已使能且面板已接线，
         * 失败不致命（无屏/屏未接时继续跑音频对话） */
        ESP_LOGI(LOG_TAG, "Starting display (APP_ENABLE_DISPLAY=y)...");
        if (!Display::get_instance().start({.task_scheduler = s_task_scheduler})) {
            ESP_LOGW(LOG_TAG, "Display start failed, continuing without screen");
        }
#endif

        s_setup_done = true;
        ESP_LOGI(LOG_TAG, "Setup done, buttons active");
    };
    auto post_result = s_task_scheduler->post(std::move(setup_task));
    BROOKESIA_CHECK_FALSE_EXIT(post_result, "Failed to post setup task");

    /* 启动按键任务（独立 FreeRTOS 任务，不占调度器 worker）
     * v22 (2026-11-03): 栈从 4KB 增到 6KB——switch_agent() 调用链较深
     * (SetTargetAgent + scheduler post + WiFi 状态检查)，小栈可能溢出导致任务静默崩溃。
     * v22c (2026-11-03): 栈分配到 PSRAM（SPIRAM）——内部 RAM 极度碎片化
     * (heap_int=1447 largest=144)，6KB 栈占内部 RAM 导致 wifi:mem fail 和 abort()。
     * 使用 xTaskCreateStaticPinnedToCore + heap_caps_aligned_alloc(MALLOC_CAP_SPIRAM)。 */
    static StackType_t *s_button_task_stack = nullptr;
    static StaticTask_t s_button_task_tcb;
    if (!s_button_task_stack) {
        s_button_task_stack = (StackType_t *)heap_caps_aligned_alloc(
                                  16, 6 * 1024 * sizeof(StackType_t),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_button_task_stack) {
            ESP_LOGE(LOG_TAG, "Failed to alloc button_task stack from PSRAM, fallback to internal");
            xTaskCreatePinnedToCore(button_task, "button_task", 6 * 1024, nullptr, 5, nullptr, 0);
        } else {
            TaskHandle_t handle = xTaskCreateStaticPinnedToCore(
                                      button_task, "button_task",
                                      6 * 1024, nullptr, 5,
                                      s_button_task_stack, &s_button_task_tcb, 0);
            if (!handle) {
                ESP_LOGE(LOG_TAG, "xTaskCreateStaticPinnedToCore failed");
                heap_caps_free(s_button_task_stack);
                s_button_task_stack = nullptr;
            }
        }
    }
}
