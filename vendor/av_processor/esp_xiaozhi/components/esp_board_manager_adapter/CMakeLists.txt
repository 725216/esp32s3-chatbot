idf_component_register(
    SRCS "esp_board_manager_adapter.c"
    INCLUDE_DIRS "include"
    REQUIRES espressif__esp_board_manager
    PRIV_REQUIRES espressif__esp_codec_dev
)
