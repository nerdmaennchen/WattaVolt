
target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
    PersistentConfig.cpp
    PersistentConfigFunctions.cpp
)

target_link_libraries(${CMAKE_PROJECT_NAME} hardware_flash)