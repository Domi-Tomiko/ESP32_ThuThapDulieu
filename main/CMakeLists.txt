idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp32-camera
        littlefs
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_http_server
        fatfs
        sdmmc
)

target_add_binary_data(${COMPONENT_LIB}
    "data/index.html"
    BINARY
)

target_add_binary_data(${COMPONENT_LIB}
    "data/style.css"
    BINARY
)

target_add_binary_data(${COMPONENT_LIB}
    "data/app.js"
    BINARY
)