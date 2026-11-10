// Runs only on the existing Network executor. No library task or radio owner.
void lunar_espnow_poll(void)
{
    if (!g_espnow_config || !g_espnow_queue) return;
    espnow_event_ctx_t event = {0};
    for (unsigned i = 0; i < 8 && xQueueReceive(g_espnow_queue, &event, 0) == pdTRUE; ++i) {
        if (event.msg_id == ESPNOW_EVENT_RECEIVE && event.data_len >= sizeof(espnow_pkt_t))
            (void)espnow_recv_process((espnow_pkt_t *)event.data, event.data_len - sizeof(espnow_pkt_t));
        else ESP_FREE(event.data);
    }
}
