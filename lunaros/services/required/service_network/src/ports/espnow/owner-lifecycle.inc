static _Atomic bool lunar_accepting;
static _Atomic unsigned lunar_callbacks;
static bool lunar_native, lunar_send_registered, lunar_recv_registered;
static portMUX_TYPE lunar_management_lock = portMUX_INITIALIZER_UNLOCKED;
static bool lunar_management_enabled;
static bool lunar_management_client;
static uint8_t lunar_management_peer[6];
static bool lunar_group_enabled,lunar_group_registered;
static uint8_t lunar_group_id[6],lunar_group_transmitter[6];

static bool lunar_admit(const espnow_data_t *packet)
{
    if (packet->type == ESPNOW_DATA_TYPE_DATA) return !packet->frame_head.security;
    bool admitted = false;
    portENTER_CRITICAL(&lunar_management_lock);
    if (lunar_group_enabled && packet->type == ESPNOW_DATA_TYPE_CONTROL_DATA &&
        packet->frame_head.group && packet->frame_head.broadcast && packet->frame_head.security &&
        !memcmp(packet->dest_addr,lunar_group_id,6) && !memcmp(packet->src_addr,lunar_group_transmitter,6)) admitted = true;
    if (lunar_management_enabled && !packet->frame_head.broadcast &&
        !memcmp(packet->dest_addr, ESPNOW_ADDR_SELF, 6) && !memcmp(packet->src_addr, lunar_management_peer, 6)) {
        switch (packet->type) {
        case ESPNOW_DATA_TYPE_SECURITY: admitted = !packet->frame_head.security; break;
        case ESPNOW_DATA_TYPE_SECURITY_STATUS: admitted = lunar_management_client && !packet->frame_head.security; break;
        case ESPNOW_DATA_TYPE_SECURITY_DATA: admitted = lunar_management_client && packet->frame_head.security; break;
        case ESPNOW_DATA_TYPE_PROV:
            admitted = packet->frame_head.security; break;
        case ESPNOW_DATA_TYPE_DEBUG_COMMAND:
        case ESPNOW_DATA_TYPE_OTA_DATA: admitted = !lunar_management_client && packet->frame_head.security; break;
        case ESPNOW_DATA_TYPE_DEBUG_LOG:
        case ESPNOW_DATA_TYPE_OTA_STATUS: admitted = lunar_management_client && packet->frame_head.security; break;
        default: break;
        }
    }
    portEXIT_CRITICAL(&lunar_management_lock);
    return admitted;
}

static void lunar_wipe(void *pointer, size_t size)
{
    mbedtls_platform_zeroize(pointer, size);
}

esp_err_t lunar_espnow_management_close(void)
{
    portENTER_CRITICAL(&lunar_management_lock);
    lunar_management_enabled = false;
    lunar_management_client = false;
    memset(lunar_management_peer, 0, 6);
    portEXIT_CRITICAL(&lunar_management_lock);
    // Callback admission closes first; crypto is used only by this same owner.
    // A failed PSA retirement keeps the context for the owner's next retry.
    esp_err_t result;
    if (g_espnow_sec) {
        if ((result = espnow_sec_deinit(g_espnow_sec)) != ESP_OK) return result;
        ESP_FREE(g_espnow_sec); g_espnow_sec = NULL;
    }
    if (g_espnow_dec) {
        if ((result = espnow_sec_deinit(g_espnow_dec)) != ESP_OK) return result;
        ESP_FREE(g_espnow_dec); g_espnow_dec = NULL;
    }
    lunar_wipe(g_espnow_sec_key, sizeof(g_espnow_sec_key));
    lunar_wipe(g_espnow_dec_key, sizeof(g_espnow_dec_key));
    g_read_from_nvs = g_read_dec_from_nvs = false;
    return ESP_OK;
}
esp_err_t lunar_espnow_management_open(const uint8_t peer[6])
{
    if (!g_espnow_config || !peer || (peer[0] & 1)) return ESP_ERR_INVALID_ARG;
    esp_err_t result = lunar_espnow_management_close();
    if (result != ESP_OK) return result;
    g_espnow_sec = ESP_CALLOC(1, sizeof(*g_espnow_sec));
    g_espnow_dec = ESP_CALLOC(1, sizeof(*g_espnow_dec));
    if (!g_espnow_sec || !g_espnow_dec) return ESP_ERR_NO_MEM;
    if ((result = espnow_sec_init(g_espnow_sec)) != ESP_OK ||
        (result = espnow_sec_init(g_espnow_dec)) != ESP_OK) return result;
    portENTER_CRITICAL(&lunar_management_lock);
    memcpy(lunar_management_peer, peer, 6); lunar_management_enabled = true;
    portEXIT_CRITICAL(&lunar_management_lock);
    return ESP_OK;
}
esp_err_t lunar_espnow_management_open_client(const uint8_t peer[6])
{
    esp_err_t result = lunar_espnow_management_open(peer);
    if (result != ESP_OK) return result;
    portENTER_CRITICAL(&lunar_management_lock);
    lunar_management_client = true;
    portEXIT_CRITICAL(&lunar_management_lock);
    return ESP_OK;
}
esp_err_t lunar_espnow_management_set_key(const uint8_t key[32])
{
    if (!g_espnow_sec || !g_espnow_dec || !key) return ESP_ERR_INVALID_STATE;
    uint8_t local[APP_KEY_LEN]; memcpy(local, key, sizeof(local));
    esp_err_t result = espnow_sec_setkey(g_espnow_sec, local);
    if (result == ESP_OK) result = espnow_sec_setkey(g_espnow_dec, local);
    if (result == ESP_OK) {
        memcpy(g_espnow_sec_key, key, APP_KEY_LEN);
        memcpy(g_espnow_dec_key, key, APP_KEY_LEN);
        g_read_from_nvs = g_read_dec_from_nvs = false;
    }
    lunar_wipe(local, sizeof(local)); return result;
}

esp_err_t lunar_espnow_group_close(void)
{
    portENTER_CRITICAL(&lunar_management_lock);
    lunar_group_enabled=false;
    portEXIT_CRITICAL(&lunar_management_lock);
    // Official group lookup also runs in the receive callback. Close ingress
    // before waiting; newly arriving callbacks cannot enter that lookup.
    if (atomic_load(&lunar_callbacks)) return ESP_ERR_TIMEOUT;
    if (lunar_group_registered) {
        esp_err_t result=espnow_del_group(lunar_group_id);
        if (result!=ESP_OK) return result;
        lunar_group_registered=false;
    }
    esp_err_t result=lunar_espnow_management_close();
    if (result!=ESP_OK) return result;
    memset(lunar_group_id,0,6);memset(lunar_group_transmitter,0,6);
    return ESP_OK;
}
esp_err_t lunar_espnow_group_open(const uint8_t group[6],const uint8_t transmitter[6],const uint8_t key[32])
{
    if (!group||!transmitter||!key) return ESP_ERR_INVALID_ARG;
    esp_err_t result=lunar_espnow_group_close();
    if (result!=ESP_OK) return result;
    if ((result=lunar_espnow_management_open(transmitter))!=ESP_OK) return result;
    portENTER_CRITICAL(&lunar_management_lock);
    lunar_management_enabled=false;
    portEXIT_CRITICAL(&lunar_management_lock);
    if ((result=lunar_espnow_management_set_key(key))!=ESP_OK) return result;
    if ((result=espnow_add_group(group))!=ESP_OK) return result;
    lunar_group_registered=true;memcpy(lunar_group_id,group,6);memcpy(lunar_group_transmitter,transmitter,6);
    portENTER_CRITICAL(&lunar_management_lock);
    lunar_group_enabled=true;
    portEXIT_CRITICAL(&lunar_management_lock);
    return ESP_OK;
}

bool lunar_espnow_has_resources(void) { return g_espnow_config != NULL; }

static void lunar_receive(const esp_now_recv_info_t *info, const uint8_t *data, int size)
{
    atomic_fetch_add(&lunar_callbacks, 1);
    if (atomic_load(&lunar_accepting) && info && info->src_addr && info->rx_ctrl && data &&
        size > (int)sizeof(espnow_data_t) && size <= (int)sizeof(espnow_data_t) + 250) {
        const espnow_data_t *packet = (const espnow_data_t *)data;
        // No forwarding or separate remote identity: preserve the SDK source MAC.
        if (lunar_admit(packet) && !packet->frame_head.ack &&
            (!packet->frame_head.group || packet->type==ESPNOW_DATA_TYPE_CONTROL_DATA) &&
            !packet->frame_head.forward_ttl &&
            !memcmp(packet->src_addr, info->src_addr, 6)) lunar_official_recv_impl(info, data, size);
    }
    atomic_fetch_sub(&lunar_callbacks, 1);
}
static void lunar_send(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    atomic_fetch_add(&lunar_callbacks, 1);
    if (atomic_load(&lunar_accepting) && info) lunar_official_send_impl(info, status);
    atomic_fetch_sub(&lunar_callbacks, 1);
}
esp_err_t espnow_init(const espnow_config_t *config)
{
    if (!config || !config->qsize || config->qsize > 8 || config->sec_enable || config->forward_enable || config->receive_enable.ack)
        return ESP_ERR_INVALID_ARG;
    if (g_espnow_config) return ESP_ERR_INVALID_STATE;
    g_espnow_config = ESP_MALLOC(sizeof(*config));
    if (!g_espnow_config) return ESP_ERR_NO_MEM;
    memcpy(g_espnow_config, config, sizeof(*config));
    // Immutable while Wi-Fi callbacks run. The strict admission gate and null
    // crypto contexts keep management disabled until a native window opens.
    g_espnow_config->sec_enable = true;
    g_set_channel_flag = false;
    g_event_group = xEventGroupCreate();
    g_send_lock = xSemaphoreCreateMutex();
    g_espnow_queue = xQueueCreate(config->qsize, sizeof(espnow_event_ctx_t));
    if (!g_event_group || !g_send_lock || !g_espnow_queue) return ESP_ERR_NO_MEM;
    memset(g_recv_handle, 0, sizeof(g_recv_handle));
    g_recv_handle[ESPNOW_DATA_TYPE_DATA].enable = true;
    esp_err_t result = esp_now_init();
    if (result != ESP_OK) return result;
    lunar_native = true;
    if ((result = esp_now_register_send_cb(lunar_send)) != ESP_OK) return result;
    lunar_send_registered = true;
    if ((result = esp_now_register_recv_cb(lunar_receive)) != ESP_OK) return result;
    lunar_recv_registered = true;
    if ((result = esp_now_set_pmk(config->pmk)) != ESP_OK ||
        (result = espnow_add_peer(ESPNOW_ADDR_BROADCAST, NULL)) != ESP_OK ||
        (result = esp_wifi_get_country(&g_self_country)) != ESP_OK ||
        (result = esp_wifi_get_mac(WIFI_IF_STA, ESPNOW_ADDR_SELF)) != ESP_OK) return result;
    atomic_store(&lunar_accepting, true);
    return ESP_OK;
}
esp_err_t espnow_deinit(void)
{
    atomic_store(&lunar_accepting, false);
    esp_err_t result;
    if (lunar_recv_registered) {
        result = esp_now_unregister_recv_cb();
        if (result != ESP_OK && result != ESP_ERR_ESPNOW_NOT_INIT) return result;
        lunar_recv_registered = false;
    }
    if (lunar_send_registered) {
        result = esp_now_unregister_send_cb();
        if (result != ESP_OK && result != ESP_ERR_ESPNOW_NOT_INIT) return result;
        lunar_send_registered = false;
    }
    if (atomic_load(&lunar_callbacks)) return ESP_ERR_TIMEOUT;
    result = lunar_espnow_group_close();
    if (result != ESP_OK) return result;
    if (lunar_native) {
        result = esp_now_deinit();
        if (result != ESP_OK && result != ESP_ERR_ESPNOW_NOT_INIT) return result;
        lunar_native = false;
    }
    if (atomic_load(&lunar_callbacks)) return ESP_ERR_TIMEOUT;
    if (g_espnow_queue) {
        espnow_event_ctx_t event;
        while (xQueueReceive(g_espnow_queue, &event, 0) == pdTRUE) ESP_FREE(event.data);
        vQueueDelete(g_espnow_queue); g_espnow_queue = NULL;
    }
    if (g_ack_queue) { vQueueDelete(g_ack_queue); g_ack_queue = NULL; }
    if (g_send_lock) { vSemaphoreDelete(g_send_lock); g_send_lock = NULL; }
    if (g_event_group) { vEventGroupDelete(g_event_group); g_event_group = NULL; }
    ESP_FREE(g_espnow_config); g_espnow_config = NULL;
    memset(g_recv_handle, 0, sizeof(g_recv_handle));
    memset(g_msg_magic_cache, 0, sizeof(g_msg_magic_cache));
    memset(g_msg_magic_sec_cache, 0, sizeof(g_msg_magic_sec_cache));
    g_msg_magic_cache_next = 0; g_buffered_num = 0;
    return ESP_OK;
}
