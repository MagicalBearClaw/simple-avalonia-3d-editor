project(renderer_test)

find_package(SDL3 CONFIG REQUIRED)

add_executable(renderer_test
    src/main.cpp
)

target_link_libraries(renderer_test
    PRIVATE
        renderer
        SDL3::SDL3
)

set_target_properties(renderer_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
