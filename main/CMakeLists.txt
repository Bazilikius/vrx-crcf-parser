idf_component_register(SRCS "main.c" "crsf.c" "elrs_backpack_emul.c" "web_server.c" "config.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_now esp_wifi esp_http_server nvs_flash)
