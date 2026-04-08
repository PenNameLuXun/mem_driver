cmake_minimum_required(VERSION 3.21)
project(MemAttribRelease LANGUAGES CXX)

set(MEMATTRIB_DRIVER_CONFIGURATION "Release" CACHE STRING "Driver build configuration")
set(MEMATTRIB_DRIVER_PLATFORM "x64" CACHE STRING "Driver build platform")
set(MEMATTRIB_RELEASE_DIR "${CMAKE_BINARY_DIR}/release" CACHE PATH "Release bundle output directory")

find_program(
    MEMATTRIB_MSBUILD
    NAMES MSBuild.exe
    PATHS
        "C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/amd64"
        "C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin"
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin/amd64"
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin"
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/MSBuild/Current/Bin/amd64"
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/MSBuild/Current/Bin"
)

if(NOT MEMATTRIB_MSBUILD)
    message(FATAL_ERROR "MSBuild.exe not found. Install Visual Studio 2022 with WDK integration first.")
endif()

add_subdirectory(user)

set(MEMATTRIB_DRIVER_SYS
    "${CMAKE_SOURCE_DIR}/build-driver/${MEMATTRIB_DRIVER_CONFIGURATION}/MemAttribDriver.sys"
)
set(MEMATTRIB_DRIVER_INF
    "${CMAKE_SOURCE_DIR}/build-driver/${MEMATTRIB_DRIVER_CONFIGURATION}/memattrib.inf"
)
set(MEMATTRIB_USER_EXE
    "$<TARGET_FILE:MemAttribCli>"
)
set(MEMATTRIB_RELEASE_ZIP
    "${CMAKE_BINARY_DIR}/MemAttrib-${MEMATTRIB_DRIVER_CONFIGURATION}.zip"
)

add_custom_target(build_driver
    COMMAND
        "${MEMATTRIB_MSBUILD}"
        "${CMAKE_SOURCE_DIR}/learn_win_driver.sln"
        /t:Build
        /p:Configuration=${MEMATTRIB_DRIVER_CONFIGURATION}
        /p:Platform=${MEMATTRIB_DRIVER_PLATFORM}
        /p:SignMode=Off
        /p:SupportsPackaging=false
    BYPRODUCTS
        "${MEMATTRIB_DRIVER_SYS}"
        "${MEMATTRIB_DRIVER_INF}"
    USES_TERMINAL
    COMMENT "Building kernel driver with MSBuild"
)

add_custom_target(build_all
    DEPENDS MemAttribCli build_driver
)

add_custom_target(release_bundle
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${MEMATTRIB_RELEASE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${MEMATTRIB_RELEASE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MEMATTRIB_USER_EXE}" "${MEMATTRIB_RELEASE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MEMATTRIB_DRIVER_SYS}" "${MEMATTRIB_RELEASE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MEMATTRIB_DRIVER_INF}" "${MEMATTRIB_RELEASE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/README.md" "${MEMATTRIB_RELEASE_DIR}/"
    DEPENDS build_all
    USES_TERMINAL
    COMMENT "Collecting release bundle"
)

add_custom_target(release_zip
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${MEMATTRIB_RELEASE_ZIP}"
    COMMAND "${CMAKE_COMMAND}" -E tar "cf" "${MEMATTRIB_RELEASE_ZIP}" --format=zip release
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    DEPENDS release_bundle
    USES_TERMINAL
    COMMENT "Creating release zip"
)

add_custom_target(publish
    DEPENDS release_zip
)
