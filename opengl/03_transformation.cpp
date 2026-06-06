#include <core/GLBaseApp.h>
#include <core/GLMesh.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <spdlog/spdlog.h>
#include <stb_image.h>

#include <array>
#include <random>
#include <stdexcept>
#include <string>

static const char* vsSource = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vColor;
out vec2 vUV;

void main() {
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
    vColor = abs(normalize(aNormal));
    vUV = aUV;
}
)";

static const char* fsSource = R"(
#version 460 core
in  vec3 vColor;
in  vec2 vUV;
out vec4 fragColor;

uniform sampler2D uTex0;
uniform sampler2D uTex1;

void main() {
    vec4 tex0 = texture(uTex0, vUV);
    vec4 tex1 = texture(uTex1, vUV);
    vec4 tint = vec4(vColor, 1.0);
    fragColor = mix(tint, mix(tex0, tex1, 0.5), 0.7);
}
)";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
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

struct TransformInfo {
    float scale = 1.0f;
    float orbitSpeed = 0.0f;
    float tiltAngle = 0.0f;
    float orbitRadius = 0.0f;
    float spinSpeed = 0.0f;
    float orbitAngle = 0.0f;
    float spinAngle = 0.0f;
};

class TransformationApp : public GLBaseApp {
    GLMesh m_cube;
    GLuint m_program = 0;
    GLuint m_tex0 = 0, m_tex1 = 0;

    GLint m_locModel = -1;
    GLint m_locView = -1;
    GLint m_locProj = -1;

    float m_mainAngle = 0.0f;
    std::array<TransformInfo, 50> m_transforms{};

public:
    TransformationApp() { m_title = "opengl-transformation"; }

private:
    void OnInit() override {
        glEnable(GL_DEPTH_TEST);

        m_cube = GLMesh::from(cube());

        GLuint vs = compileShader(GL_VERTEX_SHADER, vsSource);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSource);
        m_program = glCreateProgram();
        glAttachShader(m_program, vs);
        glAttachShader(m_program, fs);
        glLinkProgram(m_program);
        GLint linked;
        glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[512];
            glGetProgramInfoLog(m_program, 512, nullptr, log);
            spdlog::error("OpenGL program link failed: {}", log);
            throw std::runtime_error(std::string("link: ") + log);
        }
        glDeleteShader(vs);
        glDeleteShader(fs);

        m_locModel = glGetUniformLocation(m_program, "uModel");
        m_locView = glGetUniformLocation(m_program, "uView");
        m_locProj = glGetUniformLocation(m_program, "uProj");

        stbi_set_flip_vertically_on_load(true);
        m_tex0 = loadTexture(assetPath("textures/awesomeface.png").c_str());
        m_tex1 = loadTexture(assetPath("textures/container.jpg").c_str());

        glUseProgram(m_program);
        glUniform1i(glGetUniformLocation(m_program, "uTex0"), 0);
        glUniform1i(glGetUniformLocation(m_program, "uTex1"), 1);

        std::mt19937 rng(20260606);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        for (auto& info : m_transforms) {
            info.scale = 0.3f + unit(rng) * 0.5f;
            info.orbitSpeed = 200.0f + unit(rng) * 160.0f;
            info.tiltAngle = -30.0f + unit(rng) * 60.0f;
            info.orbitRadius = 2.0f + unit(rng) * 3.0f;
            info.spinSpeed = 90.0f + unit(rng) * 90.0f;
        }
    }

    void drawCube(const glm::mat4& model) const {
        glUniformMatrix4fv(m_locModel, 1, GL_FALSE, glm::value_ptr(model));
        m_cube.draw();
    }

    void OnRender() override {
        glClearColor(0.0f, 0.5f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspect = m_height > 0 ? float(m_width) / float(m_height) : 1.0f;
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
        glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 5.0f, 2.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f));

        glUseProgram(m_program);
        glUniformMatrix4fv(m_locView, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(m_locProj, 1, GL_FALSE, glm::value_ptr(proj));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_tex0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_tex1);

        m_mainAngle += 20.0f * float(m_deltaTime);
        glm::quat mainRotation = glm::angleAxis(glm::radians(m_mainAngle), glm::vec3(0, 0, 1));
        drawCube(glm::mat4_cast(mainRotation));

        for (auto& info : m_transforms) {
            info.orbitAngle += info.orbitSpeed * float(m_deltaTime);
            info.spinAngle += info.spinSpeed * float(m_deltaTime);

            glm::quat orbit = glm::angleAxis(glm::radians(info.orbitAngle), glm::vec3(0, 0, 1));
            glm::quat tilt = glm::angleAxis(glm::radians(info.tiltAngle), glm::vec3(0, 1, 0));
            glm::mat4 model =
                glm::rotate(glm::mat4(1), glm::radians(info.spinAngle), glm::vec3(0, 0, 1)) *
                glm::translate(glm::mat4(1), glm::vec3(info.orbitRadius, 0, 0)) *
                glm::mat4_cast(tilt * orbit) *
                glm::scale(glm::mat4(1), glm::vec3(info.scale));
            drawCube(model);
        }
    }

    void OnCleanup() override {
        glDeleteTextures(1, &m_tex0);
        glDeleteTextures(1, &m_tex1);
        m_cube.destroy();
        glDeleteProgram(m_program);
    }
};

int main() {
    TransformationApp app;
    app.Run();
}
