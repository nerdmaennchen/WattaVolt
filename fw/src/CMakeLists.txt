target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/")

target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
    main.cpp
#    watchdog.cpp

    buttons.cpp
    relais.cpp
    analog_readings.cpp
    output.cpp
    
    display.cpp
    logic.cpp

    misc/usbCom.cpp
    misc/usb_cdc.cpp

    test.cpp
    light_animations.cpp
)

add_subdirectory(cranc)
add_subdirectory(usb)
add_subdirectory(persistent_config)
add_subdirectory(misc)
add_subdirectory(led)
add_subdirectory(i2c)
add_subdirectory(util)
add_subdirectory(malloc)

# pull in common dependencies
target_link_libraries(${CMAKE_PROJECT_NAME} 
    pico_stdlib
    pico_unique_id
    hardware_pwm
    hardware_dma
    hardware_spi
)
