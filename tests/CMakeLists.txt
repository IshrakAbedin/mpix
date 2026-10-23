find_package(Threads REQUIRED)
include(Catch)

add_executable(tests)

target_sources(tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests.cpp
    # Second translation unit including mpix.hpp (catches duplicate symbols)
    ${CMAKE_CURRENT_SOURCE_DIR}/odr_tu.cpp
)

target_link_libraries(tests PRIVATE
    Catch2::Catch2WithMain
    mpix::mpix
    Threads::Threads
)

catch_discover_tests(tests)