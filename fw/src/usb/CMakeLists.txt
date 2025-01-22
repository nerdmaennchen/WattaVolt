target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/")

target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
    rp2040_usb_device_enumeration.cpp
    usb.cpp
)


target_link_libraries(${CMAKE_PROJECT_NAME}
    pico_unique_id
    pico_platform
)
