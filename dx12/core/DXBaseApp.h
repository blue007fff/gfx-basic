#pragma once
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <GLFW/glfw3.h>
#include <string>

class DXBaseApp {
public:
    virtual ~DXBaseApp() = default;
    void Run();

protected:
    virtual void OnInit()    = 0;
    virtual void OnRender()  = 0;
    virtual void OnCleanup() = 0;
    virtual void OnResize(int /*w*/, int /*h*/) {}
    virtual void OnKey(int /*key*/, int /*action*/) {}

    GLFWwindow*                                  m_window    = nullptr;
    HWND                                         m_hwnd      = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device>         m_device;
    Microsoft::WRL::ComPtr<IDXGISwapChain3>      m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>   m_cmdQueue;
    int         m_width   = 1280;
    int         m_height  = 720;
    std::string m_title   = "GfxBasic";
    bool        m_running = true;

private:
    void InitWindow();
    void Shutdown();
    static void OnFramebufferResize(GLFWwindow* w, int width, int height);
    static void OnKeyEvent(GLFWwindow* w, int key, int scancode, int action, int mods);
};

#endif // _WIN32
