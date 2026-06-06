# gfx-basic 트러블슈팅 노트

---

## Vulkan

### 2026-05-25: Triangle 예제 시 창이 검은색으로만 표시됨
**증상**
- 인스턴스, 서피스, 스왑체인, 디바이스, 파이프라인 모두 정상 생성
- Validation layer 에러 없음
- `vkCmdClearColorImage(0, 127, 255)` 포함 단순 렌더에도 검은 화면

```text
[VK] queue families: graphics=0, present=2
[VK] frame 0 acquired image 0 using frame slot 0
[VK] frame 0 presented image 0
```

**원인**

큐 패밀리 선택 루프가 계속 덮어써서, graphics 조건을 모두 지원하는 큐 패밀리 0 대신 present를 지원하는 큐 패밀리 2가 최종 선택됨.

```cpp
// 문제 코드
if (supportsGraphics) gfxFamily_ = i;
if (supportsPresent)  prsFamily_ = i;  // 마지막 present 지원 패밀리로 덮어씀
```

Graphics/present 큐가 분리되면 이미지 소유권 전환이 필요한데, 이를 처리하지
않아서 렌더 결과가 표시되지 않음.

**수정**

graphics + present 둘 다 지원하는 패밀리를 우선 선택:

```cpp
if (supportsGraphics && supportsPresent) {
    gfxFamily_ = i;
    prsFamily_ = i;
    break;
}
```

**디버깅에 도움이 된 것**

- 렌더 경로를 `vkCmdClearColorImage`로 줄이고 셰이더·버텍스 버퍼 제거
- GPU, 큐 패밀리, 서피스 익스텐트, 스왑체인 포맷, acquire/present 인덱스 로깅
- 스왑체인 익스텐트가 0이 아닌 `1280x720`인지 확인

**체크리스트 (Vulkan 샘플 작성 시)**

- graphics + present 둘 다 지원하는 단일 큐 패밀리 우선 사용
- 큐 패밀리 스캔 시 플래그와 `vkGetPhysicalDeviceSurfaceSupportKHR` 결과 로깅
- Vulkan 호출마다 `VK_CHECK` 사용
- 새 예제는 clear-only 프레임부터 시작한 뒤 파이프라인과 드로우 추가
- frame-in-flight 인덱스와 스왑체인 이미지 인덱스 분리
- graphics/present 큐가 다를 때 이미지 레이아웃 전환 명시적으로 처리
- 필요한 스왑체인 이미지 usage 플래그(`COLOR_ATTACHMENT`, `TRANSFER_DST` 등) 명시

---

## ImGui

### 2026-05-27: Embedded Viewport에서 InvisibleButton 없이 Image로 그리면 창이 이동됨
**증상**

`ImGui::Image()`로 FBO 텍스처를 표시하면 씬 뷰 안에서 마우스를 클릭했을 때 창이 이동되거나, 드래그가 창 타이틀 바 이동으로 해석됨.

**원인**

`ImGui::Image()`는 passive 위젯이라 마우스 클릭 입력이 배경으로 통과하고,
ImGui가 이를 창 이동 드래그로 해석함.

**패턴: InvisibleButton + DrawList**

```cpp
// 1. InvisibleButton으로 content 영역 클릭 소비
ImGui::InvisibleButton("##scene_cam", contentSize);
bool camHovered = ImGui::IsItemHovered();
bool camActive  = ImGui::IsItemActive();

// 2. DrawList로 텍스처 렌더 (레이아웃 영역 갱신 없이 겹쳐 그림)
if (fboTex) {
    ImGui::GetWindowDrawList()->AddImage(
        (ImTextureID)(intptr_t)fboTex->id(),
        pMin, pMax, ImVec2(0, 1), ImVec2(1, 0));  // Y-flip for OpenGL
}

// 3. 카메라 입력 — IsItemActive()로 드래그 시작점에 묶임
if (camHovered && io.MouseWheel != 0.f)
    camera.Zoom(io.MouseWheel * 0.05f);

if (camActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f)) {
    ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.f);
    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    // trackball 계산 ...
}
```

**IsWindowHovered() 대신 InvisibleButton을 써야 하는 이유**

| | `IsWindowHovered()` | `InvisibleButton` |
|---|---|---|
| 타이틀 바 포함 여부 | 포함됨 (버그) | 제외 |
| 드래그 시작점 추적 | 없음 | 클릭 시작점에 묶임 |
| 마우스가 영역 밖으로 나갔을 때 | 풀림 | `IsItemActive()`로 유지 |
| 창 이동 충돌 | 발생 | 없음 |

**ImGui 두 레이어 차이**

- **레이아웃 시스템** (`ImGui::Image`, `ImGui::Button` 등): 커서 기준 배치, 공간 차지, 스크롤 범위 포함
- **DrawList** (`GetWindowDrawList()->AddImage` 등): 좌표 직접 지정, 커서 이동 없음, 레이아웃 무관

`InvisibleButton`으로 공간을 확보한 상태에서 텍스처를 그릴 때는 DrawList가 적합.
`ImGui::Image`로 그리면 같은 영역에 위젯 두 개가 겹쳤다고 인식되어 레이아웃이 꼬임.

---

### 2026-05-27: GLFW 콜백 vs ImGui IO 마우스 처리 방식 선택

**두 가지 접근 방식**

**GLFW 콜백 방식** — `glfwPollEvents()` 시점 실행, ImGui::NewFrame() 이전
```cpp
void onMouseButton(int button, int action, int mods) override { ... }
void onMouseMove(double x, double y) override { ... }
```
- 이벤트 기반, 눌림/릴리즈 순간 캡처
- 상태 변수(`m_dragging`, `m_lastX/Y`) 직접 관리 필요
- 전체화면 씬 (ImGui UI 없음)에 적합

**ImGui IO 방식** — `onRender()` 안에서 매 프레임 쿼리
```cpp
ImGuiIO& io = ImGui::GetIO();
if (!io.WantCaptureMouse) {
    if (io.MouseDown[0]) { ... }
    if (io.MouseWheel != 0.f) { ... }
}
```
- `ImGui::NewFrame()` 이후 실행 — `WantCaptureMouse`가 현재 프레임 기준으로 정확
- `io.MouseDelta`로 이전 프레임 대비 이동량 바로 획득
- ImGui embedded viewport 씬에 적합

**WantCaptureMouse 체크 타이밍**

```
glfwPollEvents()   — GLFW 콜백 (이전 프레임 WantCaptureMouse, 허용으로 무방)
ImGui::NewFrame()  — WantCaptureMouse 갱신
onImGui()
onRender()         — ImGui IO 방식 (현재 프레임 기준, 더 정확)
ImGui::Render()
```

---

### 2026-06-03: DockBuilder 레이아웃이 실행할 때마다 덮어써짐

**증상**

창을 floating으로 꺼내거나 레이아웃을 수정하면 ini에 저장은 되지만,
다음 실행 시 DockBuilder가 다시 동작해 기본 레이아웃으로 초기화됨.

**원인**

`buildDefaultLayout()`이 `!node->IsLeafNode()`로 "ini에 레이아웃이 있는가"를 판단하는데, 창을 전부 floating으로 꺼내면 dockspace root가 leaf가 되어
조건을 통과하지 못하고 DockBuilder가 재실행됨.

**수정**

ini 파일 존재 여부로 판단:

```cpp
void buildDefaultLayout() {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;

    const ImGuiIO& io = ImGui::GetIO();
    if (io.IniFilename && std::filesystem::exists(io.IniFilename))
        return;  // ini가 있으면 저장된 레이아웃 그대로 사용

    // 첫 실행 시에만 기본 레이아웃 설정
    // ...DockBuilder 코드...
}
```

ini 파일이 존재하면 DockBuilder를 완전히 건너뜀. floating 창 포함 모든 상태가
그대로 복원됨.

---

## OpenGL

### 2026-06-03: FBO 리사이즈 시 GL 에러 + 화면 깨짐

**증상**

씬 뷰포트 창 크기를 바꾸면 `GL error: 0x...` 가 출력되고 해당 프레임에서 씬이 깨지거나 빈 화면이 표시됨.

**원인**

프레임 실행 순서 문제:

```
1. onImGui()  — drawSceneWindow() → AddImage(old_texture_id=5) 기록
2. onRender() — 사이즈 불일치 감지 → rebuildFBO() → texture 5 삭제, texture 6 생성
3. ImGui 렌더 — 이미 삭제된 texture 5를 그리려 시도 → GL_INVALID_OPERATION
```

`AddImage`는 texture ID (정수)만 기록할 뿐이고, `shared_ptr`로 참조하지 않음.
`onRender()`에서 FBO를 재생성하면 `onImGui()`가 기록한 texture ID가 무효화됨.

**수정**

`drawSceneWindow()`에서 `AddImage` 호출 전에 FBO를 리빌드:

```cpp
m_sceneW = (int)contentSize.x;
m_sceneH = (int)contentSize.y;

// AddImage 전에 리빌드 — onRender()에서 하면 이미 기록한 ID가 삭제됨
if (!m_fbo || !m_fbo->sameSize(m_sceneW, m_sceneH))
    rebuildFBO(m_sceneW, m_sceneH);
```

`onRender()`의 중복 리빌드 체크 제거.

**교훈**

ImGui `AddImage`/`AddImageRounded` 등은 texture ID를 값으로 복사함.
해당 프레임의 `ImGui::Render()` 시점까지 texture가 살아 있어야 하므로,
FBO 재생성은 반드시 `AddImage` 호출 전에 실행해야 함.
