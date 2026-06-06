#pragma once
#ifdef GFX_HAS_VULKAN

#include <volk.h>
#include <GLFW/glfw3.h>
#include <string>

class VKBaseApp {
public:
    virtual ~VKBaseApp() = default;
    void Run();

protected:
    virtual void OnInit()    = 0;
    virtual void OnRender()  = 0;
    virtual void OnCleanup() = 0;
    virtual void OnResize(int /*w*/, int /*h*/) {}
    virtual void OnKey(int /*key*/, int /*action*/) {}

    GLFWwindow* m_window   = nullptr;
    VkInstance  m_instance = VK_NULL_HANDLE;
    int         m_width    = 1280;
    int         m_height   = 720;
    std::string m_title    = "GfxBasic";
    bool        m_running  = true;
};

#endif // GFX_HAS_VULKAN
