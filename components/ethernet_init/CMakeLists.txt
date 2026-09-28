#set(priv_requires esp_driver_gpio esp_eth)

#if(DEFINED IDF_VERSION_MAJOR AND IDF_VERSION_MAJOR GREATER_EQUAL 6)
#    list(APPEND priv_requires ip101)
#endif()

#idf_component_register(SRCS "ethernet_init.c"
 #                      PRIV_REQUIRES ${priv_requires}
 #                      INCLUDE_DIRS ".")

set(COMPONENT_SRCDIRS ".")
set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_PRIV_REQUIRES  esp_event esp_driver_gpio esp_eth ) ## spiffs esp_http_server esp_http_client esp-tls vfs")

#set(COMPONENT_EMBED_FILES "favicon.ico")
register_component()