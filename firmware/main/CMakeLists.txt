idf_component_register(
    SRCS "main.c" "storage.c" "led.c" "wifi.c" "auth.c"
         "http_server.c" "monitor.c" "improv.c" "button.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_http_server esp_http_client
             esp_netif lwip mbedtls driver esp_timer esp_event
)
