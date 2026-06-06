#include "core/DXBaseApp.h"

#ifdef _WIN32

// GLFW에서 HWND 꺼내기 위한 native access. include 전에 매크로 정의 필수.
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <spdlog/spdlog.h>
#include <stdexcept>

void DXBaseApp::Run() {
    InitWindow();
    OnInit();
    while (!glfwWindowShouldClose(m_window) && m_running) {
        glfwPollEvents();
        OnRender();
    }
    OnCleanup();
    Shutdown();
}

void DXBaseApp::InitWindow() {
    if (!glfwInit()) {
        spdlog::error("glfwInit failed");
        throw std::runtime_error("glfwInit failed");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
    if (!m_window) {
        spdlog::error("glfwCreateWindow failed: {} ({}x{})", m_title, m_width, m_height);
        throw std::runtime_error("glfwCreateWindow failed");
    }

    m_hwnd = glfwGetWin32Window(m_window);

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, OnFramebufferResize);
    glfwSetKeyCallback(m_window, OnKeyEvent);
}

void DXBaseApp::Shutdown() {
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

void DXBaseApp::OnFramebufferResize(GLFWwindow* w, int width, int height) {
    auto* app = static_cast<DXBaseApp*>(glfwGetWindowUserPointer(w));
    app->m_width  = width;
    app->m_height = height;
    app->OnResize(width, height);
}

void DXBaseApp::OnKeyEvent(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(w, true);
    auto* app = static_cast<DXBaseApp*>(glfwGetWindowUserPointer(w));
    app->OnKey(key, action);
}

#endif // _WIN32
