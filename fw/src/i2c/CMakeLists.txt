
target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
    i2c.cpp
)

target_link_libraries(${CMAKE_PROJECT_NAME}
	hardware_i2c
)

SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fcoroutines")