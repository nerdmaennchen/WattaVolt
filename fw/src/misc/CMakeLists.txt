
target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
    hwTimer.cpp
    ISRTime.cpp
    system_reset.cpp
    systemTime.cpp
    gpio_irq_multiplexing.cpp
)


if("${PICO_BOARD}" STREQUAL "pico")
target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
    Heartbeat.cpp
)
endif()

if("${PICO_BOARD}" STREQUAL "pico_w")
target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
    Heartbeat_w.cpp
)

target_link_libraries(${CMAKE_PROJECT_NAME} 
    pico_cyw43_arch
)

endif()
