cmake_minimum_required(VERSION 3.20)

project(brookesia_hal_wasm_host_test LANGUAGES C CXX)

find_package(SDL2 REQUIRED)

add_subdirectory(.. brookesia_hal_wasm)

add_executable(brookesia_hal_wasm_ble_test
    ble_test.cpp
)
target_link_libraries(brookesia_hal_wasm_ble_test
    PRIVATE
        brookesia::hal_wasm
        SDL2::SDL2
)
set_target_properties(brookesia_hal_wasm_ble_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

enable_testing()
add_test(
    NAME brookesia_hal_wasm_ble
    COMMAND brookesia_hal_wasm_ble_test
)

brookesia_compile_tuning_apply_current_project()
