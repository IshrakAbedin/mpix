include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)

# Catch2
if(BUILD_TESTING)
    CPMAddPackage(
        URI "gh:catchorg/Catch2@3.16.1"
        GIT_SHALLOW TRUE
    )
    list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
endif()