#include "web_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "rx5808_config.h"
#include "rx5808.h"
#include <string.h>

static const char *TAG = "WEB_SERVER";

extern uint16_t rx5808_div_setup[];

/* Simple HTML template */
static const char* HTML_TEMPLATE =
"<!DOCTYPE html><html><head><title>RX5808-Div Config</title>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>body{font-family:sans-serif;margin:20px;}form{max-width:400px;}div{margin-bottom:15px;}label{display:block;margin-bottom:5px;}input,select{width:100%;padding:8px;box-sizing:border-box;}</style></head>"
"<body><h1>RX5808-Div Config</h1>"
"<form action='/save' method='POST'>"
"<div><label>CRSF Channel Video (S2):</label><input type='number' name='ch_v' value='%d' min='1' max='16'></div>"
"<div><label>CRSF Channel Band (S3):</label><input type='number' name='ch_b' value='%d' min='1' max='16'></div>"
"<div><label>L-Band Grid:</label><select name='l_grid'>"
"<option value='0' %s>Grid 1 (Standard 5362-5621)</option>"
"<option value='1' %s>Grid 2 (ELRS 5333-5613)</option>"
"</select></div>"
"<div><input type='submit' value='Save Configuration'></div>"
"</form></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req) {
    char buf[2048];
    int ch_v = rx5808_div_setup[rx5808_div_config_crsf_ch_video];
    int ch_b = rx5808_div_setup[rx5808_div_config_crsf_ch_band];
    int l_grid = rx5808_div_setup[rx5808_div_config_l_band_grid_type];

    snprintf(buf, sizeof(buf), HTML_TEMPLATE,
             ch_v, ch_b,
             l_grid == 0 ? "selected" : "",
             l_grid == 1 ? "selected" : "");

    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char val[32];
    if (httpd_query_key_value(buf, "ch_v", val, sizeof(val)) == ESP_OK) {
        rx5808_div_setup[rx5808_div_config_crsf_ch_video] = atoi(val);
        rx5808_div_setup_upload(rx5808_div_config_crsf_ch_video);
    }
    if (httpd_query_key_value(buf, "ch_b", val, sizeof(val)) == ESP_OK) {
        rx5808_div_setup[rx5808_div_config_crsf_ch_band] = atoi(val);
        rx5808_div_setup_upload(rx5808_div_config_crsf_ch_band);
    }
    if (httpd_query_key_value(buf, "l_grid", val, sizeof(val)) == ESP_OK) {
        rx5808_div_setup[rx5808_div_config_l_band_grid_type] = atoi(val);
        rx5808_div_setup_upload(rx5808_div_config_l_band_grid_type);
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
};

static const httpd_uri_t save = {
    .uri       = "/save",
    .method    = HTTP_POST,
    .handler   = save_post_handler,
};

void web_server_init(void) {
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "RX5808-Div-Config",
            .ssid_len = strlen("RX5808-Div-Config"),
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_mode(mode | WIFI_MODE_AP));
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    esp_wifi_start();

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &save);
    }
}
