project(renderer_api)

add_library(renderer_api SHARED)

target_sources(renderer_api PRIVATE
    src/renderer_api.cpp
)

target_include_directories(renderer_api
    PUBLIC  include
    PRIVATE src
)

target_compile_definitions(renderer_api PRIVATE RENDERER_C_API_EXPORTS)

target_link_libraries(renderer_api
    PRIVATE renderer
)

set_target_properties(renderer_api PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
