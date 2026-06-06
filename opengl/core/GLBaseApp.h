#pragma once
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <filesystem>
#include <string>

class GLBaseApp {
public:
    virtual ~GLBaseApp() = default;
    void Run();

    static std::string assetPath(const char* rel);

protected:
    virtual void OnInit()    = 0;
    virtual void OnRender()  = 0;
    virtual void OnCleanup() = 0;
    virtual void OnResize(int /*w*/, int /*h*/) {}
    virtual void OnKey(int /*key*/, int /*action*/) {}
    virtual void OnMouseButton(int /*button*/, int /*action*/, int /*mods*/) {}
    virtual void OnMouseMove(double /*x*/, double /*y*/) {}
    virtual void OnScroll(double /*dx*/, double /*dy*/) {}

    GLFWwindow* m_window    = nullptr;
    int         m_width     = 1280;
    int         m_height    = 720;
    std::string m_title     = "GfxBasic";
    bool        m_running   = true;
    double      m_deltaTime = 0.0;

private:
    void InitGLFW();
    void InitGlad();
    void MainLoop();
    void Shutdown();

    static void OnFramebufferResize(GLFWwindow* w, int width, int height);
    static void OnKeyEvent(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void OnMouseButtonEvent(GLFWwindow* w, int button, int action, int mods);
    static void OnCursorPosEvent(GLFWwindow* w, double x, double y);
    static void OnScrollEvent(GLFWwindow* w, double dx, double dy);

    double m_prevTime = 0.0;
};
