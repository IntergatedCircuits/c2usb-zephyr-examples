# zephyr's build system doesn't define standard build type symbols, fix that here
if(NOT CONFIG_DEBUG)
    add_definitions(-DNDEBUG)
endif()

# Use a shared CPM cache for the west workspace
set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_LIST_DIR}/../modules/cpm" PARENT_SCOPE)

add_subdirectory(lib)
