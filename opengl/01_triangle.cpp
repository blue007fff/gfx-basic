#include <core/GLBaseApp.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>

// GLSL shader sources.
// OpenGL compiles shader text at runtime, unlike the usual offline shader
// compilation flow used by DX12 and Vulkan examples.
static const char* vsSource = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
void main() {
    gl_Position = vec4(aPos, 1.0);
    vColor = aColor;
}
)";

static const char* fsSource = R"(
#version 460 core
in  vec3 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
}
)";

static GLuint compileShader(GLenum type, const char* src) {
    // Shader object는 "소스 코드 -> 컴파일된 stage" 한 개를 의미한다.
    // type이 GL_VERTEX_SHADER면 vertex stage, GL_FRAGMENT_SHADER면 fragment stage.
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        spdlog::error("OpenGL shader compile failed: {}", log);
        throw std::runtime_error(std::string("shader compile: ") + log);
    }
    spdlog::info("OpenGL shader compiled: type=0x{:x}", static_cast<unsigned>(type));
    return shader;
}

static void checkProgramLink(GLuint program) {
    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        spdlog::error("OpenGL program link failed: {}", log);
        throw std::runtime_error(std::string("program link: ") + log);
    }
}

class TriangleApp : public GLBaseApp {
    GLuint m_vao = 0, m_vbo = 0, m_program = 0;

public:
    TriangleApp() { m_title = "opengl-triangle"; }

private:
    void OnInit() override {
        // Vertex data: NDC position plus RGB color.
        // OpenGL NDC has x/y in [-1, 1] with y+ pointing up.
        float verts[] = {
            // pos                  color
             0.0f,  0.5f, 0.0f,   1.0f, 0.0f, 0.0f,  // top    - red
             0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f,  // right  - green
            -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,  // left   - blue
        };

        // VAO stores the vertex attribute layout. VBO stores the vertex data.
        // glVertexAttribPointer captures the currently bound GL_ARRAY_BUFFER.
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

        // location 0: vec3 position. stride=6*float, offset=0
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        // location 1: vec3 color. stride=6*float, offset=3*float
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // Compile shaders and link them into a program. Shader objects can be
        // deleted after linking because the program keeps its own references.
        GLuint vs = compileShader(GL_VERTEX_SHADER,   vsSource);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSource);
        m_program = glCreateProgram();
        glAttachShader(m_program, vs);
        glAttachShader(m_program, fs);
        glLinkProgram(m_program);
        checkProgramLink(m_program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        spdlog::info("OpenGL triangle initialized: VAO={}, VBO={}, program={}",
                     m_vao, m_vbo, m_program);
    }

    void OnRender() override {
        // Bind state and issue a draw call. GLBaseApp handles buffer swapping.
        glClearColor(0.0f, 127.0f / 255.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(m_program);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    void OnCleanup() override {
        glDeleteVertexArrays(1, &m_vao);
        glDeleteBuffers(1, &m_vbo);
        glDeleteProgram(m_program);
    }
};

int main() {
    TriangleApp app;
    app.Run();
}
