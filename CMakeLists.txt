# zephyr's build system doesn't define standard build type symbols, fix that here
if(NOT CONFIG_DEBUG)
    add_definitions(-DNDEBUG)
endif()
