# Coding Style

This document defines the default C++ style for GfxBasic examples and shared code.
When library, vendor, or graphics API names use a different convention, keep
the API-required name.

## Naming

- Local variables and function parameters use lower camel case.
  - Good: `ambientValue`, `frameIndex`, `lightPos`
- Member variables use `m_` plus lower camel case.
  - Good: `m_ambientValue`, `m_frameIndex`, `m_lightPos`
- Types use upper camel case.
  - Good: `TriangleApp`, `SceneObject`, `Material`
- Project-owned functions use upper camel case.
  - Good: `CreateShader()`, `LoadTexture()`, `UpdateTitle()`
- Base app callbacks and overrides also use upper camel case.
  - Good: `OnInit()`, `OnRender()`, `OnCleanup()`, `Run()`
- Constants use clear lower camel case unless they are preprocessor macros or
  API-required names.
  - Good: `maxFramesInFlight`
  - Acceptable macro/API style: `GL_COLOR_BUFFER_BIT`, `VK_NULL_HANDLE`
- Common graphics API acronyms may stay uppercase in type and function names.
  - Good: `GLBaseApp`, `DXBaseApp`, `VKBaseApp`, `CreateGPUBuffer()`
- Acronyms inside variables follow lower camel case.
  - Good: `shaderId`, `textureId`, `gpuBuffer`
  - Avoid: `shaderID`, `textureID`, `GPUBuffer`
- Boolean names should read like state or intent.
  - Good: `isDragging`, `hasDepth`, `useVsync`
  - Good member names: `m_isDragging`, `m_hasDepth`, `m_useVsync`
- Prefer consistent verbs for helper functions.
  - Create GPU/API objects: `CreateShader()`, `CreateTexture()`
  - Load external data: `LoadTexture()`, `LoadModel()`
  - Update state: `UpdateCamera()`, `UpdateTitle()`
  - Draw work: `DrawObject()`, `DrawScene()`
  - Release resources: `Destroy()` or `Cleanup()`, chosen consistently within
    a type or subsystem.

## Classes

- Classes derived from a `BaseApp` type use the `App` suffix.
  - Good: `TriangleApp`, `TextureApp`, `LightingApp`
- Keep example classes small and focused on one concept.
- Put simple helper structs near the example that owns them unless they become
  useful across multiple examples.

## Files And Targets

- Example filenames use `{number}_{example}.cpp`.
  - Good: `01_triangle.cpp`, `04_lighting.cpp`
- CMake target names use `{api}_{number}_{example}`.
  - Good: `gl_01_triangle`, `vk_01_triangle`, `dx_01_triangle`
- Window titles use lowercase `{api}-{example}`.
  - Good: `opengl-triangle`, `vulkan-triangle`, `dx12-triangle`

## Formatting

- Include order:
  1. Project headers
  2. Graphics/API headers
  3. Third-party library headers
  4. Standard library headers
- Prefer one declaration per line when initialization is non-trivial.
- Keep shader strings close to the example that uses them.
- Check shader compile and program/pipeline link or creation failures.
- Use `GLBaseApp::assetPath()` or the matching API base helper for assets.

## Example Layout

Prefer this order inside a single-file example:

1. Includes
2. Shader sources
3. Small helper functions
4. Small local structs
5. `ExampleApp` class
6. `main()`

## Resource Lifetime

- Initialize graphics handles to their invalid value.
  - OpenGL: `0`
  - Pointers: `nullptr`
  - Vulkan: `VK_NULL_HANDLE`
- Keep creation and release symmetric.
  - Create long-lived resources in `OnInit()`.
  - Release them in `OnCleanup()`.
- Do not silently ignore resource creation failures. Shader compile, program
  link, pipeline creation, device creation, swapchain creation, and asset loads
  must report errors.

## Comments

- Comments should explain graphics-specific intent, API ordering constraints, or
  non-obvious math.
- Avoid comments that only restate the code.
- Prefer explaining why an API call order, coordinate convention, sync rule, or
  matrix multiplication order matters.
- Keep comments ASCII unless the file already intentionally uses another
  encoding.
