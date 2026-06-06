add_library(opengl_core STATIC
    core/GLBaseApp.cpp
    core/GLMesh.cpp
)
target_include_directories(opengl_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(opengl_core PUBLIC
    glfw::glfw
    glad::glad
    glm::glm
    spdlog::spdlog
    stb::stb
)
if(MSVC)
    target_compile_options(opengl_core PRIVATE /utf-8)
endif()

set(GL_ASSET_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets")
add_custom_target(gl_assets
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${GL_ASSET_OUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "${GL_ASSET_OUT_DIR}")

function(add_gl_example TARGET_NAME SOURCE)
    set(PREFIXED "gl_${TARGET_NAME}")
    add_executable(${PREFIXED} "${SOURCE}")
    set_target_properties(${PREFIXED} PROPERTIES OUTPUT_NAME ${TARGET_NAME})
    target_link_libraries(${PREFIXED} PRIVATE opengl_core)
    if(WIN32)
        target_link_libraries(${PREFIXED} PRIVATE opengl32)
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(${PREFIXED} PRIVATE OpenGL::GL)
    endif()
    if(MSVC)
        target_compile_options(${PREFIXED} PRIVATE /utf-8)
        set_target_properties(${PREFIXED} PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PREFIXED}>")
    endif()
    add_dependencies(${PREFIXED} gl_assets)
endfunction()

file(GLOB GL_EXAMPLES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(SOURCE ${GL_EXAMPLES})
    get_filename_component(TARGET_NAME "${SOURCE}" NAME_WE)
    add_gl_example(${TARGET_NAME} "${SOURCE}")
endforeach()
