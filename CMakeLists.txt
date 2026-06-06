cmake_minimum_required(VERSION 3.25)

# vcpkg 툴체인 자동 감지 — project() 호출 전에 설정해야 함
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

project(GfxBasic LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/deps.cmake)

add_subdirectory(opengl)
add_subdirectory(vulkan)
add_subdirectory(dx12)
