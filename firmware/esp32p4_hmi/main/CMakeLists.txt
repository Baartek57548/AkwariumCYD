idf_component_register(
    SRCS
        "app_main.cpp"
        "aquahub_api.cpp"
        "aquahub_broker.cpp"
        "aquahub_identity.cpp"
        "aquahub_service.cpp"
        "hmi_ui.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        aquacyd_link
        aquahub_core
        esp_event
        esp_https_server
        esp_netif
        esp_system
        esp_timer
        esp-tls
        esp_wifi
        json
        mbedtls
        mdns
        mosquitto
        mqtt
        nvs_flash
)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(${LVGL_LIB} PRIVATE -DLV_LVGL_H_INCLUDE_SIMPLE)

# BSP 1.0.2 uses memcpy() but omits <string.h>. Force the standard declaration
# at compile time so the vendor component remains unmodified and reproducible.
idf_component_get_property(
    WAVESHARE_BSP_LIB
    waveshare__esp32_p4_wifi6_touch_lcd_7b
    COMPONENT_LIB
)
target_compile_options(${WAVESHARE_BSP_LIB} PRIVATE -include string.h)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)
