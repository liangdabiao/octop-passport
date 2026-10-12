// main/op_wifi.cc —— 见 op_wifi.h。esp-wifi-connect 组件的 C 封装。
// 全部 C++ 细节（WifiManager 单例、事件回调、配网热点）住在这里。
#include "op_wifi.h"

#include <string>

#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "ssid_manager.h"
#include "wifi_configuration_ap.h"
#include "wifi_manager.h"

static const char *TAG = "op_wifi";

static void (*s_on_connected)(void);
static void (*s_on_disconnected)(void);
static void (*s_custom_save)(const char *json_payload);

void op_wifi_set_custom_save(void (*cb)(const char *json_payload))
{
    s_custom_save = cb;
    WifiConfigurationAp::SetCustomSave([](const std::string &payload) {
        if (s_custom_save) s_custom_save(payload.c_str());
    });
}

void op_wifi_start(void (*on_connected)(void), void (*on_disconnected)(void))
{
    s_on_connected = on_connected;
    s_on_disconnected = on_disconnected;
    op_wifi_set_custom_save(s_custom_save);  // 重新挂一遍（幂等）。

    auto &wifi = WifiManager::GetInstance();
    WifiManagerConfig config;
    config.ssid_prefix = "OctopPassport";
    config.fixed_ap_ssid = "OctopPassport";
    config.language = "zh-CN";
    config.station_hostname = "octop-passport";
    // english-dict 同款：STA 断线重扫节流。
    config.station_scan_min_interval_seconds = 5;
    config.station_scan_max_interval_seconds = 30;
    if (!wifi.Initialize(config)) {
        ESP_LOGW(TAG, "WiFi 组件初始化失败");
        return;
    }
    wifi.SetEventCallback([](WifiEvent event, const std::string &data) {
        auto &manager = WifiManager::GetInstance();
        switch (event) {
            case WifiEvent::Connected:
                ESP_LOGI(TAG, "Wi-Fi 已连接：%s，IP=%s", data.c_str(),
                         manager.GetIpAddress().c_str());
                // english-dict 同款：连接后切高性能电源模式（modem sleep 会
                // 拖慢 raw-socket 行情拉取）。
                manager.SetPowerSaveLevel(WifiPowerSaveLevel::PERFORMANCE);
                if (s_on_connected) s_on_connected();
                break;
            case WifiEvent::Disconnected:
                ESP_LOGW(TAG, "Wi-Fi 断开");
                if (s_on_disconnected) s_on_disconnected();
                break;
            case WifiEvent::ConfigModeEnter:
                ESP_LOGI(TAG, "配网门户已开：热点=%s", manager.GetApSsid().c_str());
                break;
            case WifiEvent::ConfigModeExit:
                ESP_LOGI(TAG, "配网门户已关闭");
                break;
            default:
                ESP_LOGI(TAG, "WiFi 事件 %d: %s", (int)event, data.c_str());
                break;
        }
    });

    auto &ssid_manager = SsidManager::GetInstance();
    if (ssid_manager.GetSsidList().empty()) {
        wifi.StartConfigAp(); // 新设备：直接进配网
        ESP_LOGI(TAG, "无存档凭据，配网热点已启动");
    } else {
        wifi.StartStation();  // 老设备：自动连
    }
}

void op_wifi_open_config_ap(void)
{
    WifiManager::GetInstance().StartConfigAp();
}

void op_wifi_reset_credentials(void)
{
    auto &ssid_manager = SsidManager::GetInstance();
    auto list = ssid_manager.GetSsidList();
    for (int i = (int)list.size() - 1; i >= 0; i--) {
        ssid_manager.RemoveSsid(i); // 只清凭据，不碰应用的其他 NVS
    }
    WifiManager::GetInstance().StartConfigAp();
}

bool op_wifi_is_connected(void)
{
    return WifiManager::GetInstance().IsConnected();
}

void op_wifi_ensure_dns(void)
{
    // hot-daily 真机教训：DHCP 拿到的 DNS 会让首次 HTTP 请求超时。
    // 显式把主 DNS 设为网关、备用设为公共 DNS，双保险。
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return;

    esp_netif_dns_info_t dns1 = {};
    dns1.ip.u_addr.ip4.addr = ipaddr_addr("192.168.1.1");
    dns1.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns1);

    esp_netif_dns_info_t dns2 = {};
    dns2.ip.u_addr.ip4.addr = ipaddr_addr("223.5.5.5"); // 阿里公共 DNS
    dns2.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns2);

    ESP_LOGI(TAG, "DNS 已设置：主=192.168.1.1 备=223.5.5.5");
}
