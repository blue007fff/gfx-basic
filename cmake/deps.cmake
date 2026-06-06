include(FetchContent)

# GLFW
find_package(glfw3 CONFIG QUIET)
if(NOT glfw3_FOUND)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
        SYSTEM)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
endif()

if(NOT TARGET glfw::glfw)
    add_library(glfw_compat INTERFACE)
    if(TARGET glfw3::glfw3)
        target_link_libraries(glfw_compat INTERFACE glfw3::glfw3)
    else()
        target_link_libraries(glfw_compat INTERFACE glfw)
    endif()
    add_library(glfw::glfw ALIAS glfw_compat)
endif()

# GLM
find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE
        SYSTEM)
    FetchContent_MakeAvailable(glm)
endif()

# stb
find_path(STB_INCLUDE_DIR stb_image.h QUIET)
if(NOT STB_INCLUDE_DIR)
    FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        31c1ad37456438565541f4919958214b6e762fb4
        GIT_SHALLOW    TRUE
        SYSTEM)
    FetchContent_MakeAvailable(stb)
    set(STB_INCLUDE_DIR "${stb_SOURCE_DIR}")
endif()
if(NOT TARGET stb::stb)
    add_library(stb_headers INTERFACE)
    target_include_directories(stb_headers INTERFACE "${STB_INCLUDE_DIR}")
    add_library(stb::stb ALIAS stb_headers)
endif()

# spdlog
find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.15.1
        GIT_SHALLOW    TRUE
        SYSTEM)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(spdlog)
endif()

# GLAD2 is bundled under extern/glad.
set(GLAD_DIR "${CMAKE_SOURCE_DIR}/extern/glad")
if(NOT EXISTS "${GLAD_DIR}/src/gl.c")
    message(FATAL_ERROR
        "extern/glad/src/gl.c not found.\n"
        "Generate glad2 files at https://gen.glad.sh (OpenGL Core 4.6)\n"
        "and place them in extern/glad/")
endif()
add_library(glad_bundled STATIC "${GLAD_DIR}/src/gl.c")
target_include_directories(glad_bundled PUBLIC "${GLAD_DIR}/include")
add_library(glad::glad ALIAS glad_bundled)
