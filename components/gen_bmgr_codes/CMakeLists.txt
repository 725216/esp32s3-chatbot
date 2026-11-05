# Board information output
message(STATUS "Selected Board: custom_s3")
message(STATUS "Board Path: D:/esp32s3_chatbot/boards/custom_s3")

idf_component_register(
    SRC_DIRS "." "../../boards/custom_s3"
    INCLUDE_DIRS "." "../../boards/custom_s3"
    REQUIRES esp_board_manager
)

# This is equivalent to adding WHOLE_ARCHIVE option to the idf_component_register call above:
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
