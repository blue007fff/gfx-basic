#include <core/GLBaseApp.h>
#include <core/GLMesh.h>

#include <spdlog/spdlog.h>
#include <stb_image.h>
#include <stdexcept>
#include <string>

static const char* vsSource = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;
out vec2 vUV;
void main() {
    gl_Position = vec4(aPos, 1.0);
    vUV = aUV;
}
)";

static const char* fsSource = R"(
#version 460 core
in  vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform float     uMix;
void main() {
    vec4 c0 = texture(uTex0, vUV);
    vec4 c1 = texture(uTex1, vUV);
    fragColor = mix(c0, c1, uMix);
}
)";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
        spdlog::error("OpenGL shader compile failed: {}", log);
        throw std::runtime_error(std::string("shader: ") + log);
    }
    return s;
}

static GLuint loadTexture(const char* path) {
    int w, h, ch;
    unsigned char* data = stbi_load(path, &w, &h, &ch, 4);
    if (!data) {
        spdlog::error("Texture load failed: {}", path);
        throw std::runtime_error(std::string("stbi_load failed: ") + path);
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);
    return tex;
}

class TextureApp : public GLBaseApp {
    GLMesh m_quad;
    GLuint   m_program = 0;
    GLuint   m_tex0 = 0, m_tex1 = 0;
    float    m_mix  = 0.0f;
    GLint    m_locMix = -1;

public:
    TextureApp() { m_title = "opengl-texture"; }

private:
    void OnKey(int key, int action) override {
        if (action != GLFW_PRESS) return;
        if (key == GLFW_KEY_1) m_mix = 0.0f;
        if (key == GLFW_KEY_2) m_mix = 0.5f;
        if (key == GLFW_KEY_3) m_mix = 1.0f;
    }

    void OnInit() override {
        m_quad = GLMesh::from(quad());

        GLuint vs = compileShader(GL_VERTEX_SHADER,   vsSource);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSource);
        m_program = glCreateProgram();
        glAttachShader(m_program, vs); glAttachShader(m_program, fs);
        glLinkProgram(m_program);
        GLint linked; glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[512]; glGetProgramInfoLog(m_program, 512, nullptr, log);
            spdlog::error("OpenGL program link failed: {}", log);
            throw std::runtime_error(std::string("link: ") + log);
        }
        glDeleteShader(vs); glDeleteShader(fs);

        stbi_set_flip_vertically_on_load(true);
        m_tex0 = loadTexture(assetPath("textures/container.jpg").c_str());
        m_tex1 = loadTexture(assetPath("textures/awesomeface.png").c_str());

        glUseProgram(m_program);
        glUniform1i(glGetUniformLocation(m_program, "uTex0"), 0);
        glUniform1i(glGetUniformLocation(m_program, "uTex1"), 1);
        m_locMix = glGetUniformLocation(m_program, "uMix");
    }

    void OnRender() override {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(m_program);
        glUniform1f(m_locMix, m_mix);

        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_tex0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_tex1);

        m_quad.draw();
    }

    void OnCleanup() override {
        glDeleteTextures(1, &m_tex0);
        glDeleteTextures(1, &m_tex1);
        m_quad.destroy();
        glDeleteProgram(m_program);
    }
};

int main() {
    TextureApp app;
    app.Run();
}
