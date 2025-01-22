
if("${SKIP_PICO_MALLOC}" STREQUAL "1")

pico_add_library(my_malloc)

target_sources(my_malloc INTERFACE
    malloc.cpp
    new_delete.cpp
)
pico_wrap_function(my_malloc malloc)
pico_wrap_function(my_malloc calloc)
pico_wrap_function(my_malloc realloc)
pico_wrap_function(my_malloc free)

target_link_libraries(${CMAKE_PROJECT_NAME} 
    my_malloc
)

endif()