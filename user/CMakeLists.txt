cmake_minimum_required(VERSION 3.21)
project(MemAttribCli LANGUAGES CXX)

add_executable(MemAttribCli
    main.cpp
)

target_include_directories(MemAttribCli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_compile_features(MemAttribCli PRIVATE cxx_std_20)
target_compile_definitions(MemAttribCli PRIVATE UNICODE _UNICODE)

target_link_libraries(MemAttribCli PRIVATE dbghelp)
