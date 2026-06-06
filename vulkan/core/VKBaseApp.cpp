#include "core/VKBaseApp.h"

#ifdef GFX_HAS_VULKAN
#include <spdlog/spdlog.h>
#include <stdexcept>

void VKBaseApp::Run() {
    spdlog::info("Starting Vulkan example: {} ({}x{})", m_title, m_width, m_height);
    // volkInitialize: vkGetInstanceProcAddr를 동적 로드. 모든 Vulkan 호출 전에 필수.
    // 실패하면 시스템에 Vulkan 런타임(loader DLL/so)이 없다는 뜻.
    if (volkInitialize() != VK_SUCCESS) {
        spdlog::error("volkInitialize failed: Vulkan runtime not found");
        throw std::runtime_error("volkInitialize failed - Vulkan runtime not found");
    }
    spdlog::info("volk initialized");

    if (!glfwInit()) {
        spdlog::error("glfwInit failed");
        throw std::runtime_error("glfwInit failed");
    }
    // 핵심: Vulkan은 GLFW가 GL 컨텍스트를 만들면 안 되므로 NO_API 명시.
    // 빠뜨리면 GLFW가 OpenGL context를 생성하고 surface 만들 때 충돌난다.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
    if (!m_window) {
        spdlog::error("glfwCreateWindow failed: {} ({}x{})", m_title, m_width, m_height);
        throw std::runtime_error("glfwCreateWindow failed");
    }
    glfwSetWindowUserPointer(m_window, this);
    spdlog::info("GLFW window created for Vulkan: {}", m_title);

    // Vulkan instance and instance-level function loading happen in OnInit().
    OnInit();

    spdlog::info("Entering render loop: {}", m_title);
    while (!glfwWindowShouldClose(m_window) && m_running) {
        glfwPollEvents();
        OnRender();
    }
    OnCleanup();

    spdlog::info("Shutting down Vulkan example: {}", m_title);
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

#endif // GFX_HAS_VULKAN
