if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

set(COMPONENT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(COMPONENT_SRC_DIR ${COMPONENT_DIR}/src)
set(COMPONENT_INCLUDE_DIRS ${COMPONENT_DIR}/include)
set(COMPONENT_PRIVATE_INCLUDE_DIRS ${COMPONENT_SRC_DIR})
set(COMPONENT_REQUIRES brookesia_hal_interface brookesia_service_helper brookesia_service_manager)
set(COMPONENT_SRCS_C "")
set(COMPONENT_SRCS_CPP "")
file(GLOB COMPONENT_SRCS_C ${COMPONENT_SRC_DIR}/*.c)
file(GLOB COMPONENT_SRCS_CPP ${COMPONENT_SRC_DIR}/*.cpp)

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${COMPONENT_SRCS_C} ${COMPONENT_SRCS_CPP}
        INCLUDE_DIRS ${COMPONENT_INCLUDE_DIRS}
        PRIV_INCLUDE_DIRS ${COMPONENT_PRIVATE_INCLUDE_DIRS}
        REQUIRES ${COMPONENT_REQUIRES}
    )
    if(CONFIG_BROOKESIA_SERVICE_BT_ENABLE_AUTO_REGISTER)
        set(BROOKESIA_SERVICE_BT_PLUGIN_SYMBOL service_bt_symbol)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            BROOKESIA_SERVICE_BT_PLUGIN_SYMBOL=${BROOKESIA_SERVICE_BT_PLUGIN_SYMBOL}
        )
        target_link_libraries(${COMPONENT_LIB} PRIVATE "-u ${BROOKESIA_SERVICE_BT_PLUGIN_SYMBOL}")
    endif()
    include(package_manager)
    cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})
else()
    set(COMPONENT_LIB brookesia_service_bt)
    option(BROOKESIA_SERVICE_BT_ENABLE_AUTO_REGISTER
        "Default value of CONFIG_BROOKESIA_SERVICE_BT_ENABLE_AUTO_REGISTER on PC" ON)
    option(BROOKESIA_SERVICE_BT_PC_CONFIG_ENABLE_DEBUG_LOG
        "Default value of CONFIG_BROOKESIA_SERVICE_BT_ENABLE_DEBUG_LOG on PC" OFF)
    if(BROOKESIA_SERVICE_BT_ENABLE_AUTO_REGISTER)
        set(BROOKESIA_SERVICE_BT_PLUGIN_SYMBOL service_bt_symbol)
        set(BROOKESIA_SERVICE_BT_AUTO_REGISTER_DEFINE
            CONFIG_BROOKESIA_SERVICE_BT_ENABLE_AUTO_REGISTER=1
            BROOKESIA_SERVICE_BT_PLUGIN_SYMBOL=${BROOKESIA_SERVICE_BT_PLUGIN_SYMBOL})
    else()
        set(BROOKESIA_SERVICE_BT_AUTO_REGISTER_DEFINE CONFIG_BROOKESIA_SERVICE_BT_ENABLE_AUTO_REGISTER=0)
    endif()
    if(BROOKESIA_SERVICE_BT_PC_CONFIG_ENABLE_DEBUG_LOG)
        set(BROOKESIA_SERVICE_BT_DEBUG_DEFINE
            CONFIG_BROOKESIA_SERVICE_BT_ENABLE_DEBUG_LOG=1
            CONFIG_BROOKESIA_SERVICE_BT_SERVICE_ENABLE_DEBUG_LOG=1)
    else()
        set(BROOKESIA_SERVICE_BT_DEBUG_DEFINE
            CONFIG_BROOKESIA_SERVICE_BT_ENABLE_DEBUG_LOG=0
            CONFIG_BROOKESIA_SERVICE_BT_SERVICE_ENABLE_DEBUG_LOG=0)
    endif()

    if(NOT TARGET brookesia::lib_utils)
        add_subdirectory(${COMPONENT_DIR}/../../../utils/brookesia_lib_utils ${CMAKE_BINARY_DIR}/brookesia_lib_utils)
    endif()
    if(NOT TARGET brookesia::hal_interface)
        add_subdirectory(${COMPONENT_DIR}/../../../hal/brookesia_hal_interface ${CMAKE_BINARY_DIR}/brookesia_hal_interface)
    endif()
    if(NOT TARGET brookesia::service_manager)
        add_subdirectory(${COMPONENT_DIR}/../../framework/brookesia_service_manager ${CMAKE_BINARY_DIR}/brookesia_service_manager)
    endif()
    if(NOT TARGET brookesia::service_helper)
        add_subdirectory(${COMPONENT_DIR}/../../framework/brookesia_service_helper ${CMAKE_BINARY_DIR}/brookesia_service_helper)
    endif()
    if(NOT EMSCRIPTEN)
        find_package(Boost REQUIRED COMPONENTS thread system chrono json)
    endif()

    add_library(${COMPONENT_LIB} STATIC ${COMPONENT_SRCS_C} ${COMPONENT_SRCS_CPP})
    brookesia_define_component_version(${COMPONENT_LIB} ${COMPONENT_DIR} BROOKESIA_SERVICE_BT)
    target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
    target_include_directories(${COMPONENT_LIB} PUBLIC ${COMPONENT_INCLUDE_DIRS} PRIVATE ${COMPONENT_PRIVATE_INCLUDE_DIRS})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ${BROOKESIA_SERVICE_BT_AUTO_REGISTER_DEFINE} ${BROOKESIA_SERVICE_BT_DEBUG_DEFINE})
    target_link_libraries(${COMPONENT_LIB} PUBLIC brookesia::service_manager brookesia::service_helper brookesia::hal_interface brookesia::lib_utils)
    if(Boost_INCLUDE_DIRS)
        target_include_directories(${COMPONENT_LIB} PUBLIC ${Boost_INCLUDE_DIRS})
    endif()
    if(NOT EMSCRIPTEN)
        target_link_libraries(${COMPONENT_LIB} PUBLIC Boost::thread Boost::system Boost::chrono Boost::json)
    endif()
    if(BROOKESIA_SERVICE_BT_ENABLE_AUTO_REGISTER)
        target_link_libraries(${COMPONENT_LIB} PUBLIC "-u ${BROOKESIA_SERVICE_BT_PLUGIN_SYMBOL}")
    endif()
    add_library(brookesia::service_bt ALIAS ${COMPONENT_LIB})
endif()

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
