# GfxBasic — Agent Instructions

## Build

- 기본 컴파일러: cl (MSVC), x64-debug 프리셋
- 빌드 디렉토리: `build/x64-debug`
- cmake 실행 시 반드시 MSVC 환경 초기화
  ```
  vcvarsall.bat x64
  cmake --build build/x64-debug --target <target>
  ```
- 타깃 이름 규칙: `{api}_{example}` (예: `dx_01_triangle`, `vk_01_triangle`, `gl_01_triangle`)

## Naming

- BaseApp을 상속하는 클래스는 `~App` 형태로 사용 (예: `TriangleApp`)
- 창 타이틀: `{api}-{example}` 영문 형식 (예: `dx12-triangle`, `vulkan-triangle`, `opengl-triangle`)

## Window Management

- DX12/Vulkan/OpenGL 모두 GLFW로 창 관리 (플랫폼별 raw API 사용 금지)
- DX12는 HWND가 필요한 경우 `glfwGetWin32Window()`로 추출
