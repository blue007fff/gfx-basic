#ifdef GFX_HAS_VULKAN

#include <core/VKBaseApp.h>
#include <spdlog/spdlog.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include <vector>
#include <fstream>
#include <stdexcept>
#include <string>
#include <cstring>
#include <limits>
#include <algorithm>
#include <filesystem>

// =============================================================================
// 헬퍼들
// =============================================================================

// exe와 같은 폴더의 shaders/ 를 찾기 위한 helper.
// 작업 디렉토리(CWD) 기준 상대경로는 IDE/콘솔에서 다르게 동작하므로 위험.
static std::filesystem::path exeDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#else
    return std::filesystem::canonical("/proc/self/exe").parent_path();
#endif
}

// SPIR-V 바이너리 파일을 그대로 읽는다 (정렬 주의: uint32_t로 캐스팅 사용).
static std::vector<char> readSpv(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        spdlog::error("Cannot open SPIR-V file: {}", path.string());
        throw std::runtime_error("Cannot open: " + path.string());
    }
    size_t size = f.tellg();
    std::vector<char> buf(size);
    f.seekg(0);
    f.read(buf.data(), size);
    return buf;
}

// Vulkan API 호출 에러 체크 매크로.
// 주의: validation layer가 없어도 VkResult는 항상 와야 한다 — 무시하면 디버깅이 매우 어려워짐.
#define VK_CHECK(call) do {                                                 \
    VkResult _r = (call);                                                   \
    if (_r != VK_SUCCESS) {                                                 \
        spdlog::error("VK_CHECK failed: {} = {} (line {})", #call, static_cast<int>(_r), __LINE__); \
        throw std::runtime_error("Vulkan call failed");                     \
    }                                                                       \
} while(0)

static VkShaderModule createShaderModule(VkDevice dev, const std::vector<char>& spv) {
    VkShaderModuleCreateInfo ci = {};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spv.size();
    // pCode는 uint32_t* 라서 SPIR-V는 4바이트 정렬 필수. std::vector<char>는 정렬 보장됨.
    ci.pCode    = reinterpret_cast<const uint32_t*>(spv.data());
    VkShaderModule mod;
    VK_CHECK(vkCreateShaderModule(dev, &ci, nullptr, &mod));
    return mod;
}

static const char* formatName(VkFormat format) {
    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM: return "VK_FORMAT_B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB:  return "VK_FORMAT_B8G8R8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB:  return "VK_FORMAT_R8G8B8A8_SRGB";
    default: return "unknown";
    }
}

static const char* colorSpaceName(VkColorSpaceKHR colorSpace) {
    switch (colorSpace) {
    case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
        return "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR";
    default:
        return "unknown";
    }
}

// Validation layer가 호출하는 콜백. ERROR/WARNING만 stderr에 찍는다.
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    spdlog::error("Vulkan validation: {}", data->pMessage);
    return VK_FALSE;  // VK_TRUE면 호출 abort. 보통 false.
}

// =============================================================================
// Triangle App
// =============================================================================
// Vulkan은 명시성이 높아서 onInit이 12단계로 나뉜다:
//   1) Instance      — Vulkan loader 진입점
//   2) Surface       — GLFW 윈도우에 그릴 표면
//   3) Physical Dev  — GPU 선택 + queue family 결정
//   4) Logical Dev   — GPU 사용 위한 디바이스 + 큐
//   5) Swapchain     — 백버퍼 체인 (Present용 이미지들)
//   6) Image Views   — 스왑체인 이미지에 대한 view
//   7) Render Pass   — attachment 구성, load/store/layout 명세
//   8) Pipeline      — 셰이더 + 모든 고정 state를 묶음 (immutable)
//   9) Framebuffers  — render pass + image view 묶음
//  10) Vertex Buffer — host-visible 메모리에 정점 업로드
//  11) Command Pool  + Buffers
//  12) Sync Objects  — fence/semaphore (frames-in-flight 만큼)

static const int MAX_FRAMES_IN_FLIGHT = 2;

class TriangleApp : public VKBaseApp {
public:
    TriangleApp() {
        m_title = "vulkan-triangle";
    }

private:
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;

    VkPhysicalDevice        m_physDev   = VK_NULL_HANDLE;
    VkDevice                m_dev       = VK_NULL_HANDLE;
    VkQueue                 m_gfxQueue  = VK_NULL_HANDLE;
    VkQueue                 m_prsQueue  = VK_NULL_HANDLE;
    uint32_t                m_gfxFamily = 0, m_prsFamily = 0;

    VkSurfaceKHR             m_surface   = VK_NULL_HANDLE;
    VkSwapchainKHR           m_swapChain = VK_NULL_HANDLE;
    std::vector<VkImage>     m_swapImages;
    std::vector<VkImageView> m_swapViews;
    VkFormat                 m_swapFormat{};
    VkExtent2D               m_swapExtent{};

    VkRenderPass               m_renderPass  = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;

    VkPipelineLayout m_pipeLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline   = VK_NULL_HANDLE;

    VkBuffer       m_vertBuf = VK_NULL_HANDLE;
    VkDeviceMemory m_vertMem = VK_NULL_HANDLE;

    VkCommandPool                m_cmdPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_cmdBufs;
    // 스왑체인 이미지별 in-flight fence 추적.
    // imgIdx와 frame slot은 길이가 다르므로(스왑체인 N개, frame slot 2개)
    // 같은 이미지가 두 프레임 연속 acquire 됐을 때 안전하게 wait하기 위함.
    std::vector<VkFence>         m_imagesInFlight;
    VkSemaphore m_imgAvail[MAX_FRAMES_IN_FLIGHT]{};   // acquire 완료 신호
    VkSemaphore m_renderDone[MAX_FRAMES_IN_FLIGHT]{}; // submit 완료 신호 (present가 wait)
    VkFence     m_inFlight[MAX_FRAMES_IN_FLIGHT]{};   // CPU가 frame slot 재사용 가능한지
    int         m_frame = 0;
    int         m_loggedFrames = 0;

    // ─────────────────────────────────────────────────────────────────────────

    void OnInit() override {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createDevice();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createPipeline();
        createFramebuffers();
        createVertexBuffer();
        createCommandPool();
        createCommandBuffers();
        createSyncObjects();
    }

    // =========================================================================
    // 메인 렌더 루프
    // =========================================================================
    // 핵심 패턴:
    //   1) 이전 프레임 fence wait        — CPU side: frame slot 재사용 안전 확인
    //   2) AcquireNextImage              — 어떤 백버퍼에 그릴지 결정 (imgIdx)
    //   3) 그 이미지의 in-flight fence wait — 같은 이미지가 이전 프레임에 in-flight였다면 대기
    //   4) Reset fence + record command  — 이 시점 이후엔 GPU에 작업이 다시 들어감
    //   5) Submit (imgAvail wait → renderDone signal → inFlight fence)
    //   6) Present (renderDone wait)
    //
    // 주의: fence reset은 반드시 acquire 이후에. acquire 전에 reset하면
    //       AcquireNextImage가 OUT_OF_DATE를 돌려줬을 때 fence가 신호 안 된 채로 남는다.
    void OnRender() override {
        vkWaitForFences(m_dev, 1, &m_inFlight[m_frame], VK_TRUE, UINT64_MAX);
        uint32_t imgIdx;
        VK_CHECK(vkAcquireNextImageKHR(m_dev, m_swapChain, UINT64_MAX,
                                       m_imgAvail[m_frame], VK_NULL_HANDLE, &imgIdx));
        if (m_loggedFrames < 8) {
            spdlog::info("VK frame {} acquired image {} using frame slot {}",
                         m_loggedFrames, imgIdx, m_frame);
        }

        // 같은 이미지에 대한 이전 작업이 아직 GPU에 있을 수 있음 → 그 fence를 기다림.
        if (m_imagesInFlight[imgIdx] != VK_NULL_HANDLE) {
            VK_CHECK(vkWaitForFences(m_dev, 1, &m_imagesInFlight[imgIdx], VK_TRUE, UINT64_MAX));
        }
        m_imagesInFlight[imgIdx] = m_inFlight[m_frame];
        VK_CHECK(vkResetFences(m_dev, 1, &m_inFlight[m_frame]));

        VkCommandBuffer cb = m_cmdBufs[imgIdx];
        VK_CHECK(vkResetCommandBuffer(cb, 0));

        // ─── Command 기록 시작 ──────────────────────────────────────────────
        VkCommandBufferBeginInfo bi = {};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(cb, &bi));

        // Clear value는 RenderPass의 LOAD_OP_CLEAR가 사용한다. RGB(0,127,255).
        VkClearValue clear = {};
        clear.color.float32[0] = 0.0f;
        clear.color.float32[1] = 127.0f / 255.0f;
        clear.color.float32[2] = 1.0f;
        clear.color.float32[3] = 1.0f;

        // RenderPass 시작: attachment를 UNDEFINED → COLOR_ATTACHMENT_OPTIMAL로 자동 전환.
        // (RenderPass의 dependency가 이 transition을 보장한다)
        VkRenderPassBeginInfo rbi = {};
        rbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rbi.renderPass      = m_renderPass;
        rbi.framebuffer     = m_framebuffers[imgIdx];
        rbi.renderArea      = {{0, 0}, m_swapExtent};
        rbi.clearValueCount = 1;
        rbi.pClearValues    = &clear;
        vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &m_vertBuf, &offset);

        // Viewport/Scissor는 dynamic state로 선언했으므로 매 프레임 설정해야 함.
        // 주의: Vulkan은 y+가 아래. 위로 뒤집고 싶으면 height에 음수를 넣고 y에 height를 더한다.
        VkViewport vp = {0, 0, (float)m_swapExtent.width, (float)m_swapExtent.height, 0, 1};
        VkRect2D sc = {{0, 0}, m_swapExtent};
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);

        vkCmdDraw(cb, 3, 1, 0, 0);  // vertexCount=3, instanceCount=1
        vkCmdEndRenderPass(cb);     // PRESENT_SRC_KHR로 자동 transition (finalLayout)
        VK_CHECK(vkEndCommandBuffer(cb));

        // ─── Submit ─────────────────────────────────────────────────────────
        // waitStage: imgAvail 세마포어가 신호될 때까지 *이 스테이지에서* 멈춤.
        // COLOR_ATTACHMENT_OUTPUT_BIT이면 vertex/fragment 처리는 미리 진행 가능.
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = {};
        si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount   = 1;
        si.pWaitSemaphores      = &m_imgAvail[m_frame];
        si.pWaitDstStageMask    = &waitStage;
        si.commandBufferCount   = 1;
        si.pCommandBuffers      = &cb;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores    = &m_renderDone[m_frame];
        VK_CHECK(vkQueueSubmit(m_gfxQueue, 1, &si, m_inFlight[m_frame]));

        // ─── Present ────────────────────────────────────────────────────────
        // renderDone semaphore가 신호되어야 화면에 올림.
        VkPresentInfoKHR pi = {};
        pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores    = &m_renderDone[m_frame];
        pi.swapchainCount     = 1;
        pi.pSwapchains        = &m_swapChain;
        pi.pImageIndices      = &imgIdx;
        VK_CHECK(vkQueuePresentKHR(m_prsQueue, &pi));
        if (m_loggedFrames < 8) {
            spdlog::info("VK frame {} presented image {}", m_loggedFrames, imgIdx);
            m_loggedFrames++;
        }

        m_frame = (m_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void OnCleanup() override {
        // 진행 중 작업 끝날 때까지 대기. 안 하면 in-use 리소스 destroy로 크래시.
        vkDeviceWaitIdle(m_dev);

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(m_dev, m_imgAvail[i],   nullptr);
            vkDestroySemaphore(m_dev, m_renderDone[i], nullptr);
            vkDestroyFence(m_dev, m_inFlight[i], nullptr);
        }
        vkDestroyCommandPool(m_dev, m_cmdPool, nullptr);
        for (auto fb : m_framebuffers) vkDestroyFramebuffer(m_dev, fb, nullptr);
        vkDestroyPipeline(m_dev, m_pipeline, nullptr);
        vkDestroyPipelineLayout(m_dev, m_pipeLayout, nullptr);
        vkDestroyRenderPass(m_dev, m_renderPass, nullptr);
        for (auto v : m_swapViews) vkDestroyImageView(m_dev, v, nullptr);
        vkDestroySwapchainKHR(m_dev, m_swapChain, nullptr);
        vkFreeMemory(m_dev, m_vertMem, nullptr);
        vkDestroyBuffer(m_dev, m_vertBuf, nullptr);
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        if (m_debugMessenger)
            vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        vkDestroyDevice(m_dev, nullptr);
        vkDestroyInstance(m_instance, nullptr);
    }

    // =========================================================================
    // 초기화 단계들
    // =========================================================================

    // ─── 1) Instance + Validation Layer ─────────────────────────────────────
    // Instance: Vulkan loader 진입점. 어떤 extension/layer를 쓸지 여기서 결정.
    // VK_LAYER_KHRONOS_validation은 SDK에 포함된 디버깅 필수 도구.
    // 주의: 릴리즈 빌드에선 보통 끈다 (성능 영향 큼).
    void createInstance() {
        VkApplicationInfo ai = {};
        ai.sType      = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.apiVersion = VK_API_VERSION_1_0;

        // GLFW가 surface를 만들기 위해 필요한 instance extension들.
        // 그 외에 디버깅용 VK_EXT_debug_utils 추가.
        uint32_t glfwExtCount;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
        std::vector<const char*> exts(glfwExts, glfwExts + glfwExtCount);
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        const char* layers[] = {"VK_LAYER_KHRONOS_validation"};

        VkInstanceCreateInfo ci = {};
        ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo        = &ai;
        ci.enabledExtensionCount   = (uint32_t)exts.size();
        ci.ppEnabledExtensionNames = exts.data();
        ci.enabledLayerCount       = 1;
        ci.ppEnabledLayerNames     = layers;
        VK_CHECK(vkCreateInstance(&ci, nullptr, &m_instance));

        // volk: 함수 포인터 로더. instance를 만든 직후 instance-level 함수들을 로드해야
        // vkGetPhysicalDeviceXxx 같은 호출이 동작한다.
        volkLoadInstance(m_instance);

        // Debug messenger: validation layer 메시지를 받기 위한 콜백 등록.
        VkDebugUtilsMessengerCreateInfoEXT dbgCI = {};
        dbgCI.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        dbgCI.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgCI.pfnUserCallback = debugCallback;
        vkCreateDebugUtilsMessengerEXT(m_instance, &dbgCI, nullptr, &m_debugMessenger);
    }

    // ─── 2) Surface ─────────────────────────────────────────────────────────
    // OS 윈도우와 Vulkan을 잇는 추상화. GLFW가 플랫폼별 처리를 대신해준다.
    void createSurface() {
        VK_CHECK(glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface));
    }

    // ─── 3) Physical Device + Queue Family 선택 ─────────────────────────────
    // GPU 1개 선택 + graphics/present queue family 결정.
    // 우선순위: graphics+present를 모두 지원하는 단일 family.
    // 둘이 분리되어 있어도 동작하긴 하지만 swapchain에서 CONCURRENT 모드가 필요하다.
    //
    // 주의: queue family는 GPU의 "용도별 큐 그룹". 같은 family 안의 큐들은 동일 기능.
    //       graphics는 거의 항상 0번 family지만 가정하지 말고 enum해야 한다.
    void pickPhysicalDevice() {
        uint32_t count;
        vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devs(count);
        vkEnumeratePhysicalDevices(m_instance, &count, devs.data());

        for (auto d : devs) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(d, &props);

            uint32_t qCount;
            vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, nullptr);
            std::vector<VkQueueFamilyProperties> qProps(qCount);
            vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, qProps.data());

            bool foundCombined = false;
            bool foundGfx = false, foundPrs = false;
            uint32_t fallbackGfx = 0, fallbackPrs = 0;

            for (uint32_t i = 0; i < qCount; i++) {
                VkBool32 present = VK_FALSE;
                VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(d, i, m_surface, &present));
                const bool graphics = (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
                spdlog::info("VK queue family {} flags=0x{:08x} present={}",
                             i, qProps[i].queueFlags, static_cast<uint32_t>(present));

                if (graphics && present) {
                    m_gfxFamily = i;
                    m_prsFamily = i;
                    foundCombined = true;
                    break;
                }
                if (graphics && !foundGfx) {
                    fallbackGfx = i;
                    foundGfx = true;
                }
                if (present && !foundPrs) {
                    fallbackPrs = i;
                    foundPrs = true;
                }
            }
            if (!foundCombined && foundGfx && foundPrs) {
                m_gfxFamily = fallbackGfx;
                m_prsFamily = fallbackPrs;
            }
            if (foundCombined || (foundGfx && foundPrs)) {
                m_physDev = d;
                spdlog::info("VK selected GPU: {}", props.deviceName);
                spdlog::info("VK queue families: graphics={}, present={}",
                             m_gfxFamily, m_prsFamily);
                break;
            }
        }
        if (m_physDev == VK_NULL_HANDLE) {
            spdlog::error("No suitable Vulkan GPU found");
            throw std::runtime_error("No suitable GPU found");
        }
    }

    // ─── 4) Logical Device + Queue ──────────────────────────────────────────
    // physical device 위에서 실제로 작업하는 핸들.
    // graphics/present family가 같으면 큐 1개, 다르면 2개 만든다 (중복 방지 로직).
    // VK_KHR_swapchain extension은 swapchain 사용에 필수.
    void createDevice() {
        float prio = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> qCIs;
        for (uint32_t fam : {m_gfxFamily, m_prsFamily}) {
            bool already = false;
            for (auto& q : qCIs) if (q.queueFamilyIndex == fam) { already = true; break; }
            if (!already) {
                VkDeviceQueueCreateInfo qi = {};
                qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                qi.queueFamilyIndex = fam;
                qi.queueCount       = 1;
                qi.pQueuePriorities = &prio;
                qCIs.push_back(qi);
            }
        }
        const char* ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo ci = {};
        ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.queueCreateInfoCount    = (uint32_t)qCIs.size();
        ci.pQueueCreateInfos       = qCIs.data();
        ci.enabledExtensionCount   = 1;
        ci.ppEnabledExtensionNames = &ext;
        VK_CHECK(vkCreateDevice(m_physDev, &ci, nullptr, &m_dev));

        // volk: device-level 함수 포인터 로드. instance-level과 별도로 호출 필수.
        // 안 하면 vkCmdXxx, vkQueueSubmit 등이 nullptr 호출로 크래시.
        volkLoadDevice(m_dev);
        vkGetDeviceQueue(m_dev, m_gfxFamily, 0, &m_gfxQueue);
        vkGetDeviceQueue(m_dev, m_prsFamily, 0, &m_prsQueue);
    }

    // ─── 5) Swapchain ───────────────────────────────────────────────────────
    // 백버퍼 이미지 체인 생성.
    // 핵심 결정사항:
    //   - format/colorSpace: surface가 지원하는 것 중에서 선택
    //   - extent: caps.currentExtent (0xFFFFFFFF면 우리가 결정)
    //   - imageCount: minImageCount + 1 권장 (acquire 블로킹 줄임)
    //   - presentMode: FIFO_KHR (vsync, 항상 지원). MAILBOX는 저지연이지만 옵션.
    //   - sharingMode: queue family 분리 시 CONCURRENT 필수
    //
    // 주의: TRANSFER_DST_BIT은 vkCmdClearColorImage / blit 용도. 명시적 clear 패턴에 필요.
    void createSwapchain() {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physDev, m_surface, &caps);
        spdlog::info(
            "VK surface currentExtent={}x{} minExtent={}x{} maxExtent={}x{} usageFlags=0x{:08x}",
            caps.currentExtent.width, caps.currentExtent.height,
            caps.minImageExtent.width, caps.minImageExtent.height,
            caps.maxImageExtent.width, caps.maxImageExtent.height,
            caps.supportedUsageFlags);

        // Surface format 선택: B8G8R8A8_UNORM + sRGB nonlinear가 가장 호환성 좋음.
        // 못 찾으면 R8G8B8A8_UNORM으로 fallback. 둘 다 없으면 첫 번째 formats[0] 사용.
        uint32_t fmtCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physDev, m_surface, &fmtCount, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physDev, m_surface, &fmtCount, fmts.data());

        VkSurfaceFormatKHR chosen = fmts[0];
        for (auto& f : fmts) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosen = f; break;
            }
        }
        if (chosen.format != VK_FORMAT_B8G8R8A8_UNORM) {
            for (auto& f : fmts) {
                if (f.format == VK_FORMAT_R8G8B8A8_UNORM &&
                    f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    chosen = f; break;
                }
            }
        }
        m_swapFormat = chosen.format;
        spdlog::info("VK swapchain format = {} ({}), colorSpace = {} ({})",
                     formatName(chosen.format), static_cast<int>(chosen.format),
                     colorSpaceName(chosen.colorSpace), static_cast<int>(chosen.colorSpace));

        // Extent: currentExtent가 0xFFFFFFFF면 우리가 정할 수 있음 (보통 윈도우 크기).
        m_swapExtent = caps.currentExtent.width != UINT32_MAX
                    ? caps.currentExtent
                    : VkExtent2D{(uint32_t)m_width, (uint32_t)m_height};
        spdlog::info("VK swapchain extent = {}x{}", m_swapExtent.width, m_swapExtent.height);

        uint32_t imgCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0) imgCount = std::min(imgCount, caps.maxImageCount);

        VkSwapchainCreateInfoKHR ci = {};
        ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface          = m_surface;
        ci.minImageCount    = imgCount;
        ci.imageFormat      = m_swapFormat;
        ci.imageColorSpace  = chosen.colorSpace;
        ci.imageExtent      = m_swapExtent;
        ci.imageArrayLayers = 1;  // VR/스테레오일 때만 >1
        ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.preTransform     = caps.currentTransform;
        ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;  // vsync. 항상 지원되는 안전한 기본값.
        ci.clipped          = VK_TRUE;

        // graphics와 present가 다른 family일 때 동시 접근을 허용하려면 CONCURRENT.
        // 같은 family면 EXCLUSIVE가 더 빠르다.
        if (m_gfxFamily != m_prsFamily) {
            uint32_t families[] = {m_gfxFamily, m_prsFamily};
            ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices   = families;
        } else {
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        VK_CHECK(vkCreateSwapchainKHR(m_dev, &ci, nullptr, &m_swapChain));

        // 실제 생성된 이미지 개수를 다시 조회 (요청값과 다를 수 있음).
        vkGetSwapchainImagesKHR(m_dev, m_swapChain, &imgCount, nullptr);
        m_swapImages.resize(imgCount);
        vkGetSwapchainImagesKHR(m_dev, m_swapChain, &imgCount, m_swapImages.data());
        spdlog::info("VK swapchain images: {}", imgCount);
    }

    // ─── 6) Image Views ─────────────────────────────────────────────────────
    // VkImage는 raw 메모리. 셰이더나 attachment로 쓰려면 View 필요 (format/aspect 명세).
    void createImageViews() {
        m_swapViews.resize(m_swapImages.size());
        for (size_t i = 0; i < m_swapImages.size(); i++) {
            VkImageViewCreateInfo ci = {};
            ci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ci.image    = m_swapImages[i];
            ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ci.format   = m_swapFormat;
            ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(m_dev, &ci, nullptr, &m_swapViews[i]));
        }
    }

    // ─── 7) Render Pass ─────────────────────────────────────────────────────
    // attachment의 load/store/layout 변화를 미리 선언한다.
    // - loadOp=CLEAR: BeginRenderPass 시 clearValue로 채움
    // - initialLayout=UNDEFINED: 이전 내용 보존 안 함 (성능↑)
    // - finalLayout=PRESENT_SRC_KHR: EndRenderPass 시 present 가능 상태로 transition
    //
    // 주의: subpass dependency가 image acquisition 후 layout transition 타이밍을 결정한다.
    //       srcSubpass=EXTERNAL → dstSubpass=0 dependency가 있어야
    //       imgAvail semaphore가 신호된 후에 RT write가 시작된다.
    void createRenderPass() {
        VkAttachmentDescription att = {};
        att.format         = m_swapFormat;
        att.samples        = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub = {};
        sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments    = &ref;

        VkSubpassDependency dep = {};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo ci = {};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 1;
        ci.pAttachments    = &att;
        ci.subpassCount    = 1;
        ci.pSubpasses      = &sub;
        ci.dependencyCount = 1;
        ci.pDependencies   = &dep;
        VK_CHECK(vkCreateRenderPass(m_dev, &ci, nullptr, &m_renderPass));
    }

    // ─── 8) Graphics Pipeline ───────────────────────────────────────────────
    // DX12 PSO와 동일 개념: 셰이더 + 모든 고정 state를 immutable한 하나의 객체로 묶음.
    // 상태 조합 수만큼 pipeline이 필요해 캐싱이 중요.
    //
    // 주의: pVertexInputState와 정점 버퍼의 stride/format이 1바이트라도 어긋나면
    //       validation에 안 잡히고 화면이 깨지거나 검게 나올 수 있다.
    void createPipeline() {
        // SPIR-V는 빌드 타임에 glslang으로 컴파일되어 exe 옆 shaders/에 복사됨 (CMake).
        auto shaderDir = exeDir() / "shaders";
        spdlog::info("VK shader dir: {}", shaderDir.string());
        auto vertSpv = readSpv(shaderDir / "triangle.vert.spv");
        auto fragSpv = readSpv(shaderDir / "triangle.frag.spv");
        spdlog::info("VK shaders loaded: vert={} bytes, frag={} bytes",
                     vertSpv.size(), fragSpv.size());
        VkShaderModule vertMod = createShaderModule(m_dev, vertSpv);
        VkShaderModule fragMod = createShaderModule(m_dev, fragSpv);

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertMod;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragMod;
        stages[1].pName  = "main";

        // Vertex layout: vec2 pos (offset 0) + vec3 color (offset 8). stride = 5*float = 20.
        VkVertexInputBindingDescription binding = {0, 5 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[2] = {
            {0, 0, VK_FORMAT_R32G32_SFLOAT,    0},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 8},
        };
        VkPipelineVertexInputStateCreateInfo vi = {};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = &binding;
        vi.vertexAttributeDescriptionCount = 2;
        vi.pVertexAttributeDescriptions    = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia = {};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        // Viewport/Scissor는 dynamic state로 빼서 매 프레임 설정.
        // pViewports/pScissors는 nullptr로 두고 count만 명시.
        VkPipelineViewportStateCreateInfo vpState = {};
        vpState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpState.viewportCount = 1;
        vpState.scissorCount  = 1;

        // 학습용으로 culling OFF. 실전에선 BACK_BIT + 일관된 winding이 일반적.
        // 주의: Vulkan 기본 frontFace는 COUNTER_CLOCKWISE (DX12와 반대).
        VkPipelineRasterizationStateCreateInfo rast = {};
        rast.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL;
        rast.cullMode    = VK_CULL_MODE_NONE;
        rast.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rast.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms = {};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAtt = {};
        blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend = {};
        blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments    = &blendAtt;

        // Dynamic state로 viewport/scissor 선언 → 매 vkCmdSetViewport/Scissor 필수.
        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn = {};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates    = dynStates;

        // Pipeline layout: descriptor set / push constant 레이아웃. 여기선 비어 있음.
        VkPipelineLayoutCreateInfo plci = {};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        VK_CHECK(vkCreatePipelineLayout(m_dev, &plci, nullptr, &m_pipeLayout));

        VkGraphicsPipelineCreateInfo ci = {};
        ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        ci.stageCount          = 2;
        ci.pStages             = stages;
        ci.pVertexInputState   = &vi;
        ci.pInputAssemblyState = &ia;
        ci.pViewportState      = &vpState;
        ci.pRasterizationState = &rast;
        ci.pMultisampleState   = &ms;
        ci.pColorBlendState    = &blend;
        ci.pDynamicState       = &dyn;
        ci.layout              = m_pipeLayout;
        ci.renderPass          = m_renderPass;
        VK_CHECK(vkCreateGraphicsPipelines(m_dev, VK_NULL_HANDLE, 1, &ci, nullptr, &m_pipeline));
        spdlog::info("VK pipeline created OK");

        // 셰이더 모듈은 pipeline 생성 후 해제 가능 (pipeline이 내부적으로 들고 있음).
        vkDestroyShaderModule(m_dev, vertMod, nullptr);
        vkDestroyShaderModule(m_dev, fragMod, nullptr);
    }

    // ─── 9) Framebuffers ────────────────────────────────────────────────────
    // RenderPass의 attachment slot에 실제 ImageView를 묶는다. 스왑체인 이미지마다 1개.
    void createFramebuffers() {
        m_framebuffers.resize(m_swapViews.size());
        for (size_t i = 0; i < m_swapViews.size(); i++) {
            VkFramebufferCreateInfo ci = {};
            ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass      = m_renderPass;
            ci.attachmentCount = 1;
            ci.pAttachments    = &m_swapViews[i];
            ci.width           = m_swapExtent.width;
            ci.height          = m_swapExtent.height;
            ci.layers          = 1;
            VK_CHECK(vkCreateFramebuffer(m_dev, &ci, nullptr, &m_framebuffers[i]));
        }
    }

    // ─── 10) Vertex Buffer ──────────────────────────────────────────────────
    // 학습용으로 HOST_VISIBLE+HOST_COHERENT 메모리에 직접 업로드.
    // 실전에선 DEVICE_LOCAL에 staging buffer로 복사하는 게 표준 (속도 차이 큼).
    //
    // 주의: VkBuffer 생성 → MemoryRequirements 조회 → 적합한 memory type 검색 →
    //       Allocate → Bind 순서. 이 순서를 어기면 동작 안 한다.
    void createVertexBuffer() {
        struct Vertex { float pos[2]; float col[3]; };
        // Vulkan NDC: y+가 아래. 위 꼭짓점은 y=-0.5.
        Vertex verts[] = {
            {{ 0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},  // top   - red
            {{ 0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}},  // right - green
            {{-0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},  // left  - blue
        };
        VkDeviceSize size = sizeof(verts);

        VkBufferCreateInfo bi = {};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = size;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(m_dev, &bi, nullptr, &m_vertBuf));

        // MemoryRequirements: 정렬/크기/허용 memory type bitmask를 알려준다.
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_dev, m_vertBuf, &req);

        // HOST_VISIBLE: CPU에서 Map 가능. HOST_COHERENT: flush 없이 가시.
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(m_physDev, &memProps);
        uint32_t memType = UINT32_MAX;
        VkMemoryPropertyFlags needed = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((req.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & needed) == needed) {
                memType = i; break;
            }
        }
        if (memType == UINT32_MAX) {
            spdlog::error("No suitable memory type for vertex buffer");
            throw std::runtime_error("No suitable memory type for vertex buffer");
        }

        VkMemoryAllocateInfo ai = {};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = memType;
        VK_CHECK(vkAllocateMemory(m_dev, &ai, nullptr, &m_vertMem));
        VK_CHECK(vkBindBufferMemory(m_dev, m_vertBuf, m_vertMem, 0));

        // Map → memcpy → Unmap. COHERENT가 아니면 vkFlushMappedMemoryRanges 호출이 필요.
        void* data;
        VK_CHECK(vkMapMemory(m_dev, m_vertMem, 0, size, 0, &data));
        memcpy(data, verts, size);
        vkUnmapMemory(m_dev, m_vertMem);
    }

    // ─── 11) Command Pool + Buffers ─────────────────────────────────────────
    // CommandPool은 특정 queue family 전용. 다른 family에 submit하면 검증 에러.
    // RESET_COMMAND_BUFFER_BIT: 개별 buffer를 vkResetCommandBuffer 가능.
    void createCommandPool() {
        VkCommandPoolCreateInfo ci = {};
        ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = m_gfxFamily;
        VK_CHECK(vkCreateCommandPool(m_dev, &ci, nullptr, &m_cmdPool));
    }

    // 스왑체인 이미지 개수만큼 command buffer 생성. 이미지 인덱스로 직접 lookup.
    void createCommandBuffers() {
        m_cmdBufs.resize(m_swapImages.size());
        m_imagesInFlight.assign(m_swapImages.size(), VK_NULL_HANDLE);
        VkCommandBufferAllocateInfo ai = {};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = m_cmdPool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = (uint32_t)m_cmdBufs.size();
        VK_CHECK(vkAllocateCommandBuffers(m_dev, &ai, m_cmdBufs.data()));
    }

    // ─── 12) Sync Objects ───────────────────────────────────────────────────
    // - Semaphore: GPU↔GPU 동기화 (queue 간). acquire→render, render→present.
    // - Fence:     GPU→CPU 동기화. CPU가 frame slot 재사용 시점을 안다.
    //
    // 주의: inFlight fence는 SIGNALED 상태로 생성해야 첫 프레임의 vkWaitForFences가 즉시 통과한다.
    void createSyncObjects() {
        VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo     fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                    nullptr, VK_FENCE_CREATE_SIGNALED_BIT};
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VK_CHECK(vkCreateSemaphore(m_dev, &si, nullptr, &m_imgAvail[i]));
            VK_CHECK(vkCreateSemaphore(m_dev, &si, nullptr, &m_renderDone[i]));
            VK_CHECK(vkCreateFence(m_dev, &fi, nullptr, &m_inFlight[i]));
        }
    }
};

int main() {
    try {
        TriangleApp app;
        app.Run();
    } catch (const std::exception& e) {
        spdlog::critical("Fatal error: {}", e.what());
        return 1;
    }
}

#else
int main() { return 0; }
#endif
