
target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
    ./init/systemInitializer.cpp
    ./msg/MessagePump.cpp
    ./timer/swTimer.cpp
    ./coro/SwitchToMainLoop.cpp
)
