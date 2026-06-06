if(NOT WIN32)
    message(STATUS "DX12 is Windows-only; dx12 targets skipped")
    return()
endif()

add_library(dx12_core STATIC
    core/DXBaseApp.cpp
)
target_include_directories(dx12_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(dx12_core PUBLIC
    glfw::glfw
    spdlog::spdlog
    d3d12
    dxgi
    d3dcompiler
)
if(MSVC)
    target_compile_options(dx12_core PRIVATE /utf-8)
endif()

function(add_dx_example TARGET_NAME SOURCE)
    set(PREFIXED "dx_${TARGET_NAME}")
    add_executable(${PREFIXED} "${SOURCE}")
    set_target_properties(${PREFIXED} PROPERTIES OUTPUT_NAME ${TARGET_NAME})
    target_link_libraries(${PREFIXED} PRIVATE dx12_core)
    if(MSVC)
        target_compile_options(${PREFIXED} PRIVATE /utf-8)
    endif()
endfunction()

file(GLOB DX_EXAMPLES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(SOURCE ${DX_EXAMPLES})
    get_filename_component(TARGET_NAME "${SOURCE}" NAME_WE)
    add_dx_example(${TARGET_NAME} "${SOURCE}")
endforeach()
