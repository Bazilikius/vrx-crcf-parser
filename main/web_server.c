#include "web_server.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "WEB_SERVER";

static const char* HTML_TEMPLATE =
"<!DOCTYPE html><html><head><title>Backpack Emul Config</title>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>body{font-family:sans-serif;margin:20px;}div{margin-bottom:15px;}label{display:block;}input,select{width:100%;padding:8px;}</style></head>"
"<body><h1>Config</h1><form action='/save' method='POST'>"
"<div><label>UID (HEX):</label><input type='text' name='uid' value='%02X%02X%02X%02X%02X%02X'></div>"
"<div><label>Video CH (S2):</label><input type='number' name='ch_v' value='%d'></div>"
"<div><label>Band CH (S3):</label><input type='number' name='ch_b' value='%d'></div>"
"<div><label>L-Band:</label><select name='l_grid'><option value='0' %s>Std</option><option value='1' %s>ELRS</option></select></div>"
"<input type='submit' value='Save'></form></body></html>";

static esp_err_t root_handler(httpd_req_t *req) {
    char buf[1024];
    snprintf(buf, sizeof(buf), HTML_TEMPLATE,
             device_config.uid[0], device_config.uid[1], device_config.uid[2],
             device_config.uid[3], device_config.uid[4], device_config.uid[5],
             device_config.ch_v, device_config.ch_b,
             device_config.l_grid == 0 ? "selected" : "",
             device_config.l_grid == 1 ? "selected" : "");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t save_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char val[32];
    if (httpd_query_key_value(buf, "uid", val, sizeof(val)) == ESP_OK) {
        for(int i=0; i<6; i++) sscanf(val + i*2, "%02hhX", &device_config.uid[i]);
    }
    if (httpd_query_key_value(buf, "ch_v", val, sizeof(val)) == ESP_OK) device_config.ch_v = atoi(val);
    if (httpd_query_key_value(buf, "ch_b", val, sizeof(val)) == ESP_OK) device_config.ch_b = atoi(val);
    if (httpd_query_key_value(buf, "l_grid", val, sizeof(val)) == ESP_OK) device_config.l_grid = atoi(val);

    config_save();
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

void web_server_init(void) {
    wifi_config_t cfg = {.ap = {.ssid = "Backpack-Emul", .channel = 1, .authmode = WIFI_AUTH_OPEN, .max_connection = 4}};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));

    httpd_handle_t server = NULL;
    httpd_config_t h_cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &h_cfg) == ESP_OK) {
        httpd_uri_t r = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
        httpd_uri_t s = {.uri = "/save", .method = HTTP_POST, .handler = save_handler};
        httpd_register_uri_handler(server, &r);
        httpd_register_uri_handler(server, &s);
    }
}
