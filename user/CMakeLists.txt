cmake_minimum_required(VERSION 3.21)
project(MemAttribCli LANGUAGES C CXX)

add_executable(MemAttribCli
    main.cpp
)

add_executable(SegmentLayoutDemo
    segment_demo.c
)

target_include_directories(MemAttribCli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_compile_features(MemAttribCli PRIVATE cxx_std_20)
target_compile_definitions(MemAttribCli PRIVATE UNICODE _UNICODE)

target_link_libraries(MemAttribCli PRIVATE dbghelp)

set_target_properties(SegmentLayoutDemo PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)
