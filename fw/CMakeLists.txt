cmake_minimum_required(VERSION 3.25)

set(PICO_BOARD pico)
set(PICO_PLATFORM rp2040)

set(SKIP_PICO_MALLOC 1) 

include(pico_sdk_import.cmake)
# include(pico_extras_import.cmake)

project(my_psu)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

pico_sdk_init()

add_executable(${CMAKE_PROJECT_NAME})
#pico_set_binary_type(${CMAKE_PROJECT_NAME} copy_to_ram)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC "src/")
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fcoroutines -Wno-psabi")
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wno-psabi -DPICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1")

# pull in common dependencies
target_link_libraries(${CMAKE_PROJECT_NAME} 
        pico_stdlib
)


set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/memmap_w_config.ld)

# create map/bin/hex file etc.
pico_add_extra_outputs(${CMAKE_PROJECT_NAME})

add_subdirectory(src)

add_subdirectory(lvgl)
target_include_directories(lvgl PUBLIC src/lvgl)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(my_psu lvgl)
