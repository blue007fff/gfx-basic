#include "core/GLBaseApp.h"
#define STB_IMAGE_IMPLEMENTATION
#include <spdlog/spdlog.h>
#include <stb_image.h>
#include <stdexcept>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

static std::filesystem::path exeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#else
    return std::filesystem::canonical("/proc/self/exe").parent_path();
#endif
}

std::string GLBaseApp::assetPath(const char* rel) {
    return (exeDir() / "assets" / rel).string();
}

void GLBaseApp::Run() {
    spdlog::info("Starting OpenGL example: {} ({}x{})", m_title, m_width, m_height);
    InitGLFW();
    InitGlad();
    m_prevTime = glfwGetTime();
    OnInit();
    MainLoop();
    OnCleanup();
    Shutdown();
}

void GLBaseApp::InitGLFW() {
    if (!glfwInit()) {
        spdlog::error("glfwInit failed");
        throw std::runtime_error("glfwInit failed");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
    if (!m_window) {
        spdlog::error("glfwCreateWindow failed: {} ({}x{})", m_title, m_width, m_height);
        throw std::runtime_error("glfwCreateWindow failed");
    }

    glfwMakeContextCurrent(m_window);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, OnFramebufferResize);
    glfwSetKeyCallback(m_window, OnKeyEvent);
    glfwSetMouseButtonCallback(m_window, OnMouseButtonEvent);
    glfwSetCursorPosCallback(m_window, OnCursorPosEvent);
    glfwSetScrollCallback(m_window, OnScrollEvent);
    glfwSwapInterval(1);
    spdlog::info("GLFW window created for OpenGL: {} (vsync on)", m_title);
}

void GLBaseApp::InitGlad() {
    if (!gladLoadGL(glfwGetProcAddress)) {
        spdlog::error("gladLoadGL failed");
        throw std::runtime_error("gladLoadGL failed");
    }
    spdlog::info("OpenGL context ready: {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
}

void GLBaseApp::MainLoop() {
    spdlog::info("Entering render loop: {}", m_title);
    while (!glfwWindowShouldClose(m_window) && m_running) {
        glfwPollEvents();

        const double now = glfwGetTime();
        m_deltaTime = now - m_prevTime;
        m_prevTime = now;

        OnRender();
        glfwSwapBuffers(m_window);
    }
}

void GLBaseApp::Shutdown() {
    spdlog::info("Shutting down OpenGL example: {}", m_title);
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

void GLBaseApp::OnFramebufferResize(GLFWwindow* w, int width, int height) {
    glViewport(0, 0, width, height);
    auto* app = static_cast<GLBaseApp*>(glfwGetWindowUserPointer(w));
    app->m_width = width;
    app->m_height = height;
    spdlog::info("OpenGL framebuffer resized: {}x{}", width, height);
    app->OnResize(width, height);
}

void GLBaseApp::OnKeyEvent(GLFWwindow* w, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(w, true);
    auto* app = static_cast<GLBaseApp*>(glfwGetWindowUserPointer(w));
    app->OnKey(key, action);
}

void GLBaseApp::OnMouseButtonEvent(GLFWwindow* w, int button, int action, int mods) {
    static_cast<GLBaseApp*>(glfwGetWindowUserPointer(w))->OnMouseButton(button, action, mods);
}

void GLBaseApp::OnCursorPosEvent(GLFWwindow* w, double x, double y) {
    static_cast<GLBaseApp*>(glfwGetWindowUserPointer(w))->OnMouseMove(x, y);
}

void GLBaseApp::OnScrollEvent(GLFWwindow* w, double dx, double dy) {
    static_cast<GLBaseApp*>(glfwGetWindowUserPointer(w))->OnScroll(dx, dy);
}
