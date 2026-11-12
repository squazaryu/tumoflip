set(RX_COMMON "${CMAKE_CURRENT_LIST_DIR}/../../../../applications_user/tumo_rx_link")
idf_component_register(SRCS "main.c" "${RX_COMMON}/rx_link.c"
                       INCLUDE_DIRS "${RX_COMMON}"
                       REQUIRES driver esp_timer)
