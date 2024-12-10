idf_component_register(SRCS "CryptoDisplay.c"
    REQUIRES driver esp32-i2c-lcd1602 esp32-smbus esp_event esp_wifi nvs_flash esp_http_client esp-tls
    INCLUDE_DIRS "."
    EMBED_TXTFILES server_root_cert.pem)
