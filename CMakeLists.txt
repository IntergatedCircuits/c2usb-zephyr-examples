# until https://github.com/zephyrproject-rtos/zephyr/pull/69490 is merged
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpermissive")

# zephyr's build system doesn't define standard build type symbols, fix that here
if(NOT CONFIG_DEBUG)
    add_definitions(-DNDEBUG)
endif()
