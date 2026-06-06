find_package(volk CONFIG QUIET)
if(NOT volk_FOUND)
    message(STATUS "volk not found; vulkan targets skipped")
    return()
endif()

add_library(vulkan_core STATIC
    core/VKBaseApp.cpp
)
target_include_directories(vulkan_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vulkan_core PUBLIC
    glfw::glfw
    spdlog::spdlog
    volk::volk
)
target_compile_definitions(vulkan_core PUBLIC GFX_HAS_VULKAN)
if(MSVC)
    target_compile_options(vulkan_core PRIVATE /utf-8)
endif()

find_program(GLSLC glslc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
          "C:/VulkanSDK/1.4.350.0/Bin")
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
          "C:/VulkanSDK/1.4.350.0/Bin")

set(VK_SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${VK_SHADER_OUT_DIR}")

file(GLOB_RECURSE VK_GLSL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.geom"
)

set(VK_SPV_FILES)
foreach(GLSL ${VK_GLSL_SOURCES})
    get_filename_component(GLSL_NAME "${GLSL}" NAME)
    set(SPV "${VK_SHADER_OUT_DIR}/${GLSL_NAME}.spv")
    if(GLSLC)
        add_custom_command(OUTPUT "${SPV}"
            COMMAND ${GLSLC} "${GLSL}" -o "${SPV}"
            DEPENDS "${GLSL}"
            COMMENT "glslc: ${GLSL_NAME} -> ${GLSL_NAME}.spv")
    elseif(GLSLANG_VALIDATOR)
        add_custom_command(OUTPUT "${SPV}"
            COMMAND ${GLSLANG_VALIDATOR} -V "${GLSL}" -o "${SPV}"
            DEPENDS "${GLSL}"
            COMMENT "glslangValidator: ${GLSL_NAME} -> ${GLSL_NAME}.spv")
    else()
        message(WARNING "No Vulkan shader compiler found; shaders will not be compiled automatically")
    endif()
    list(APPEND VK_SPV_FILES "${SPV}")
endforeach()

if(VK_SPV_FILES)
    add_custom_target(vk_shaders ALL DEPENDS ${VK_SPV_FILES})
endif()

function(add_vk_example TARGET_NAME SOURCE)
    set(PREFIXED "vk_${TARGET_NAME}")
    add_executable(${PREFIXED} "${SOURCE}")
    set_target_properties(${PREFIXED} PROPERTIES OUTPUT_NAME ${TARGET_NAME})
    target_link_libraries(${PREFIXED} PRIVATE vulkan_core)
    if(MSVC)
        target_compile_options(${PREFIXED} PRIVATE /utf-8)
    endif()
    if(TARGET vk_shaders)
        add_dependencies(${PREFIXED} vk_shaders)
    endif()
endfunction()

file(GLOB VK_EXAMPLES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(SOURCE ${VK_EXAMPLES})
    get_filename_component(TARGET_NAME "${SOURCE}" NAME_WE)
    add_vk_example(${TARGET_NAME} "${SOURCE}")
endforeach()
