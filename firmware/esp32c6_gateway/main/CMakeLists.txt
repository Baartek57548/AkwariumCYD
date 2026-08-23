set(aquahub_certificate "")
if(CONFIG_AQUACYD_MQTT_EMBED_HUB_CERTIFICATE)
    set(aquahub_certificate "aquahub.pem")
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${aquahub_certificate}")
        message(FATAL_ERROR
            "AquaHub certificate missing. Run tools/pin-aquahub-certificate.ps1 first.")
    endif()
endif()

idf_component_register(
    SRCS
        "app_main.cpp"
    INCLUDE_DIRS
        "."
    EMBED_TXTFILES
        ${aquahub_certificate}
    REQUIRES
        aquacyd_link
        esp_app_format
        esp_event
        esp_netif
        esp_wifi
        esp_timer
        json
        mqtt
        nvs_flash
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)
