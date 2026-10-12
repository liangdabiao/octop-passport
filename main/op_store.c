// main/op_store.c —— 见 op_store.h。
#include "op_store.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "op_store";
static const char *NS = "octop";

esp_err_t op_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t get_str(nvs_handle_t h, const char *key, char *out, size_t cap, size_t *len)
{
    size_t need = cap;
    const esp_err_t err = nvs_get_str(h, key, out, &need);
    if (err == ESP_OK && len) *len = need;
    return err;
}

esp_err_t op_store_load_config(op_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    get_str(h, "url", cfg->url, sizeof(cfg->url), NULL);
    get_str(h, "user", cfg->user, sizeof(cfg->user), NULL);
    get_str(h, "pass", cfg->pass, sizeof(cfg->pass), NULL);
    get_str(h, "stt_url", cfg->stt_url, sizeof(cfg->stt_url), NULL);
    get_str(h, "stt_key", cfg->stt_key, sizeof(cfg->stt_key), NULL);
    nvs_close(h);
    return cfg->url[0] ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t op_store_save_config(const op_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if ((err = nvs_set_str(h, "url", cfg->url)) == ESP_OK &&
        (err = nvs_set_str(h, "user", cfg->user)) == ESP_OK &&
        (err = nvs_set_str(h, "pass", cfg->pass)) == ESP_OK &&
        (err = nvs_set_str(h, "stt_url", cfg->stt_url)) == ESP_OK &&
        (err = nvs_set_str(h, "stt_key", cfg->stt_key)) == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGW(TAG, "保存配置失败: %s", esp_err_to_name(err));
    return err;
}

bool op_store_has_config(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    char scratch[OP_STORE_URL_MAX];
    const bool ok = get_str(h, "url", scratch, sizeof(scratch), NULL) == ESP_OK &&
                    nvs_get_str(h, "user", scratch, &(size_t){ sizeof(scratch) }) == ESP_OK;
    nvs_close(h);
    return ok;
}

esp_err_t op_store_load_jwt(char *jwt, size_t cap, int64_t *issued_at)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = get_str(h, "jwt", jwt, cap, NULL);
    if (err == ESP_OK && issued_at) {
        err = nvs_get_i64(h, "jwt_iat", issued_at);
        if (err == ESP_ERR_NVS_NOT_FOUND) { *issued_at = 0; err = ESP_OK; }
    }
    nvs_close(h);
    return err;
}

esp_err_t op_store_save_jwt(const char *jwt, int64_t issued_at)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if ((err = nvs_set_str(h, "jwt", jwt)) == ESP_OK &&
        (err = nvs_set_i64(h, "jwt_iat", issued_at)) == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t op_store_load_agent(char *agent_id, size_t cap)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = get_str(h, "agent", agent_id, cap, NULL);
    nvs_close(h);
    return err;
}

esp_err_t op_store_save_agent(const char *agent_id)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if ((err = nvs_set_str(h, "agent", agent_id)) == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t op_store_load_thread(char *thread_id, size_t cap)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = get_str(h, "thread", thread_id, cap, NULL);
    nvs_close(h);
    return err;
}

esp_err_t op_store_save_thread(const char *thread_id)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if ((err = nvs_set_str(h, "thread", thread_id)) == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t op_store_clear_thread(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_erase_key(h, "thread");  // 不存在也算清干净。
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}
