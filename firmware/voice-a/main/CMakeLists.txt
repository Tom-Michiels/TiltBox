idf_component_register(SRCS "games.c" "board.c" "voice.c" "embed_api.c"
    INCLUDE_DIRS "." REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s
    esp_driver_rmt esp_adc esp_timer esp_pm nvs_flash micropython
    esp_wifi esp_netif esp_event esp_http_client mbedtls json lwip esp_partition esp_hw_support esp_driver_usb_serial_jtag)
