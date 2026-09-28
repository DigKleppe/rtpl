# Embed the server root certificate into the final binary
idf_build_get_property(project_dir PROJECT_DIR)

set(COMPONENT_SRCDIRS ".")                                       
set(COMPONENT_ADD_INCLUDEDIRS "include")
#set(COMPONENT_PRIV_REQUIRES  "main spiffs" "nvs_flash esp_event app_update esp_http_client esp_https_ota driver mbedtls esp_wifi wifiConnect spiffsOTA")
set(PRIV_REQUIRES esp_driver_i2s esp_driver_gpio esp_driver_i2c esp32_p4_platform esp_codec_dev )
set (COMPONENT_EMBED_TXTFILES "canon.pcm")
register_component()
#spiffs_create_partition_image(storage ../spiffs_image FLASH_IN_PROJECT)

#idf_component_register(SRCS "eth.c" "i2s_es8311_example.c"
#                    PRIV_REQUIRES esp_driver_i2s esp_driver_gpio esp_driver_i2c esp32_p4_platform esp_codec_dev EMBED_FILES "canon.pcm")