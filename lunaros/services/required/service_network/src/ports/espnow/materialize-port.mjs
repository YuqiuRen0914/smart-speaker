import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {createHash} from 'node:crypto';
import path from 'node:path';
const root=path.resolve(process.argv[3]), output=process.argv[2];
if(!output)throw new Error('Expected build-only materialization directory');
const identity=JSON.parse(readFileSync(path.join(root,'UPSTREAM.json'),'utf8'));
for(const [name,hash]of Object.entries(identity.files))if(createHash('sha256').update(readFileSync(path.join(root,'upstream',name))).digest('hex')!==hash)throw new Error('Official original differs: '+name);
let source=readFileSync(path.join(root,'upstream/src/espnow/src/espnow.c'),'utf8');
function once(before,after){if(source.split(before).length!==2)throw new Error('Official ESP-NOW port anchor changed');source=source.replace(before,after);}
function region(start,end,replacement){const a=source.indexOf(start),b=source.indexOf(end,a+start.length);if(a<0||b<0||source.indexOf(start,a+start.length)>=0)throw new Error('Official ESP-NOW function boundary changed');source=source.slice(0,a)+replacement+'\n\n'+source.slice(b);}
once('#include <stdlib.h>','#include <stdlib.h>\n#include <stdatomic.h>\n#include <mbedtls/platform_util.h>');
once('static uint32_t g_buffered_num;','static _Atomic uint32_t g_buffered_num;');
once('    bool enable;\n    handler_for_data_t handle;','    _Atomic bool enable;\n    handler_for_data_t handle;');
source=source.replace(/ESP_LOG[DV]\([\s\S]*?\);/g,log=>log.replaceAll('espnow_data->payload','"<binary>"'));
once('    ESP_LOG_BUFFER_HEXDUMP(TAG, data, size, ESP_LOG_DEBUG);','    /* Binary protocol data never enters ordinary diagnostics logs. */');
source=source.replaceAll('    ESP_LOG_BUFFER_HEX_LEVEL(TAG, key_info, APP_KEY_LEN, ESP_LOG_DEBUG);','    /* Session keys never enter logs. */');
once('void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int size)','static void lunar_official_recv_impl(const esp_now_recv_info_t *recv_info, const uint8_t *data, int size)');
once('void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)','static void lunar_official_send_impl(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)');
once('    g_buffered_num ++;','    g_buffered_num ++;\n    if (!wait_ticks && !frame_head->ack) return ESP_OK; /* accepted SDK enqueue, same asynchronous public contract */');
once('        espnow_set_key(key_info);','        ret = espnow_set_key(key_info);\n        if (ret != ESP_OK) { ESP_FREE(espnow_data); return ret; }');
once('                    espnow_set_dec_key(key_info);','                    ret = espnow_set_dec_key(key_info);\n                    if (ret != ESP_OK) goto EXIT;');
once('    ESP_FREE(q_data);\n    ESP_FREE(data);\n    return ret;',
  '    ESP_FREE(q_data);\n    if (data) mbedtls_platform_zeroize(data, data_cap);\n    ESP_FREE(data);\n    return ret;');
region('static void espnow_main_task(void *arg)','static void wifi_event_handler',readFileSync(path.join(import.meta.dirname,'owner-poll.inc'),'utf8'));
region('esp_err_t espnow_init(const espnow_config_t *config)','esp_err_t espnow_set_config_for_data_type',readFileSync(path.join(import.meta.dirname,'owner-lifecycle.inc'),'utf8'));
mkdirSync(output,{recursive:true});writeFileSync(path.join(output,'espnow.c'),source);
let group=readFileSync(path.join(root,'upstream/src/espnow/src/espnow_group.c'),'utf8');
const groupAllocation='    item = ESP_MALLOC(sizeof(espnow_group_item_t));';
if(group.split(groupAllocation).length!==2)throw new Error('Official group allocation anchor changed');
group=group.replace(groupAllocation,groupAllocation+'\n    if (!item) return ESP_ERR_NO_MEM;');
writeFileSync(path.join(output,'espnow_group.c'),group);
// Preserve failed key retirement rather than discarding the only PSA handle.
let security=readFileSync(path.join(root,'upstream/src/security/src/espnow_security.c'),'utf8');
const destroy='        (void)psa_destroy_key(ctx->key_id);';
if(security.split(destroy).length!==3)throw new Error('Official ESP-NOW PSA retirement anchor changed');
security=security.replaceAll(destroy,'        if (psa_destroy_key(ctx->key_id) != PSA_SUCCESS) return ESP_FAIL;');
writeFileSync(path.join(output,'espnow_security.c'),security);
// The official client is serialized by Network, never its upstream batch task.
// Keep upstream bytes fixed and derive only ownership/retirement fixes here.
let client=readFileSync(path.join(root,'upstream/src/security/src/protocomm/security/client_security1.c'),'utf8');
function clientOnce(before,after){if(client.split(before).length!==2)throw new Error('Official security1 client anchor changed');client=client.replace(before,after);}
clientOnce('#include "protocomm_client_security1.h"','#include <mbedtls/version.h>\n#include <mbedtls/platform_util.h>\n#include "protocomm_client_security1.h"');
clientOnce('    ESP_LOGD(TAG, "%s:", msg);\n    ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, len, ESP_LOG_DEBUG);','    (void)msg; (void)buf; (void)len; /* Never log key material. */');
for(const name of ['session->keypair_id','session->key_id','cur_session->key_id']){
  const anchor=`(void)psa_destroy_key(${name});`;
  if(!client.includes(anchor))throw new Error('Official client key retirement changed');
  client=client.replaceAll(anchor,`if (psa_destroy_key(${name}) != PSA_SUCCESS) return ESP_FAIL;`);
}
clientOnce('    if (cur_session->state == SESSION_STATE_DONE) {\n        /* Free AES context data */','    {\n        /* Partial handshakes own AES resources too. */');
clientOnce('        (void)psa_cipher_abort(&cur_session->ctx_aes);','        if (psa_cipher_abort(&cur_session->ctx_aes) != PSA_SUCCESS) return ESP_FAIL;');
clientOnce('    (void)psa_cipher_abort(&session->ctx_aes);','    if (psa_cipher_abort(&session->ctx_aes) != PSA_SUCCESS) { free(outbuf); return ESP_FAIL; }');
clientOnce('        if (psa_destroy_key(session->key_id) != PSA_SUCCESS) return ESP_FAIL;',
  '        if (psa_destroy_key(session->key_id) != PSA_SUCCESS) { free(outbuf); return ESP_FAIL; }');
clientOnce('        sec1_close_session(handle, session_id);','        if (sec1_close_session(handle, cur_session->id) != ESP_OK) return ESP_FAIL;');
clientOnce('    memset(cur_session, 0, sizeof(session_t));','    mbedtls_platform_zeroize(cur_session, sizeof(session_t));');
clientOnce('    memset(session, 0, sizeof(public_session_t));','    mbedtls_platform_zeroize(session, sizeof(public_session_t));');
clientOnce('        *outlen = 0;\n        *outbuf = NULL;\n        return ESP_OK;','        *outlen = 0;\n        *outbuf = NULL;\n        session_data__free_unpacked(req, NULL);\n        return ESP_OK;');
const cleanupStart=client.indexOf('static esp_err_t sec1_cleanup('),cleanupEnd=client.indexOf('\n#if ESP_IDF_VERSION',cleanupStart);
if(cleanupStart<0||cleanupEnd<0)throw new Error('Official client cleanup boundary changed');
client=client.slice(0,cleanupStart)+`static esp_err_t sec1_cleanup(protocomm_security_handle_t handle)
{
    session_t *session = (session_t *)handle;
    if (session && sec1_close_session(handle, session->id) != ESP_OK) return ESP_FAIL;
    if (pub_session) {
        if (pub_session_deinit(pub_session) != ESP_OK) return ESP_FAIL;
        pub_session = NULL;
    }
    free(handle);
    return ESP_OK;
}
`+client.slice(cleanupEnd);
writeFileSync(path.join(output,'client_security1.c'),client);
