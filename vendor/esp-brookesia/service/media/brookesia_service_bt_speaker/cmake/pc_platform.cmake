set(COMPONENT_LIB brookesia_service_bt_speaker)
set(component_pc_config_compile_definitions "")

option(
    BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_AUTO_REGISTER
    "Default value of CONFIG_BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_AUTO_REGISTER on PC"
    ON
)
option(
    BROOKESIA_SERVICE_BT_SPEAKER_PC_CONFIG_ENABLE_DEBUG_LOG
    "Default value of CONFIG_BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_DEBUG_LOG on PC"
    OFF
)

if(BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_AUTO_REGISTER)
    set(BROOKESIA_SERVICE_BT_SPEAKER_PLUGIN_SYMBOL service_bt_speaker_symbol)
    list(APPEND component_pc_config_compile_definitions
        CONFIG_BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_AUTO_REGISTER=1
        BROOKESIA_SERVICE_BT_SPEAKER_PLUGIN_SYMBOL=${BROOKESIA_SERVICE_BT_SPEAKER_PLUGIN_SYMBOL}
    )
else()
    list(APPEND component_pc_config_compile_definitions
        CONFIG_BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_AUTO_REGISTER=0
    )
endif()

if(BROOKESIA_SERVICE_BT_SPEAKER_PC_CONFIG_ENABLE_DEBUG_LOG)
    list(APPEND component_pc_config_compile_definitions
        CONFIG_BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_DEBUG_LOG=1
        CONFIG_BROOKESIA_SERVICE_BT_SPEAKER_SERVICE_ENABLE_DEBUG_LOG=1
    )
else()
    list(APPEND component_pc_config_compile_definitions
        CONFIG_BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_DEBUG_LOG=0
        CONFIG_BROOKESIA_SERVICE_BT_SPEAKER_SERVICE_ENABLE_DEBUG_LOG=0
    )
endif()

if(NOT TARGET brookesia::lib_utils)
    add_subdirectory(
        ${COMPONENT_DIR}/../../../utils/brookesia_lib_utils
        ${CMAKE_BINARY_DIR}/brookesia_lib_utils
    )
endif()
if(NOT TARGET brookesia::service_manager)
    add_subdirectory(
        ${COMPONENT_DIR}/../../framework/brookesia_service_manager
        ${CMAKE_BINARY_DIR}/brookesia_service_manager
    )
endif()
if(NOT TARGET brookesia::service_helper)
    add_subdirectory(
        ${COMPONENT_DIR}/../../framework/brookesia_service_helper
        ${CMAKE_BINARY_DIR}/brookesia_service_helper
    )
endif()
if(NOT TARGET brookesia::service_bt)
    add_subdirectory(
        ${COMPONENT_DIR}/../../network/brookesia_service_bt
        ${CMAKE_BINARY_DIR}/brookesia_service_bt
    )
endif()

if(NOT EMSCRIPTEN)
    find_package(Boost REQUIRED COMPONENTS thread system chrono json)
endif()

add_library(${COMPONENT_LIB} STATIC ${COMPONENT_SRCS_C} ${COMPONENT_SRCS_CPP})
brookesia_define_component_version(
    ${COMPONENT_LIB} ${COMPONENT_DIR} BROOKESIA_SERVICE_BT_SPEAKER
)
add_library(brookesia::service_bt_speaker ALIAS ${COMPONENT_LIB})

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
target_include_directories(${COMPONENT_LIB}
    PUBLIC ${COMPONENT_INCLUDE_DIRS}
    PRIVATE ${COMPONENT_PRIVATE_INCLUDE_DIRS}
)
if(Boost_INCLUDE_DIRS)
    target_include_directories(${COMPONENT_LIB} PUBLIC ${Boost_INCLUDE_DIRS})
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ${component_pc_config_compile_definitions}
)
target_link_libraries(${COMPONENT_LIB}
    PUBLIC
        brookesia::service_manager
        brookesia::service_helper
        brookesia::lib_utils
    PRIVATE
        brookesia::service_bt
)
if(NOT EMSCRIPTEN)
    target_link_libraries(${COMPONENT_LIB}
        PUBLIC Boost::thread Boost::system Boost::chrono Boost::json
    )
endif()
if(BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_AUTO_REGISTER)
    target_link_libraries(${COMPONENT_LIB} PUBLIC "-u ${BROOKESIA_SERVICE_BT_SPEAKER_PLUGIN_SYMBOL}")
endif()

if(DEFINED BROOKESIA_SERVICE_BT_SPEAKER_PC_COMPILE_DEFINITIONS AND
    NOT "${BROOKESIA_SERVICE_BT_SPEAKER_PC_COMPILE_DEFINITIONS}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        ${BROOKESIA_SERVICE_BT_SPEAKER_PC_COMPILE_DEFINITIONS}
    )
endif()
if(DEFINED BROOKESIA_SERVICE_BT_SPEAKER_PC_COMPILE_OPTIONS AND
    NOT "${BROOKESIA_SERVICE_BT_SPEAKER_PC_COMPILE_OPTIONS}" STREQUAL "")
    target_compile_options(${COMPONENT_LIB} PRIVATE
        ${BROOKESIA_SERVICE_BT_SPEAKER_PC_COMPILE_OPTIONS}
    )
endif()
