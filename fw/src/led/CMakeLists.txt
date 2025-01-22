target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
    ws2812.cpp
)


target_link_libraries(${CMAKE_PROJECT_NAME}
    hardware_pio
)

pico_generate_pio_header(${CMAKE_PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR})