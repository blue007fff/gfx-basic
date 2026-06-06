#include <core/GLBaseApp.h>
#include <core/GLMesh.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <spdlog/spdlog.h>
#include <cmath>
#include <stdexcept>
#include <string>

static const char* vsSource = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vFragPos;
out vec3 vNormal;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos    = worldPos.xyz;
    // Normal은 위치처럼 translation을 받으면 안 된다.
    // non-uniform scale까지 안전하게 처리하려면 inverse-transpose normal matrix를 사용한다.
    vNormal     = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uProj * uView * worldPos;
}
)";

static const char* fsSource = R"(
#version 460 core
in  vec3 vFragPos;
in  vec3 vNormal;
out vec4 fragColor;

uniform vec3  uLightPos;
uniform vec3  uViewPos;
uniform vec3  uAlbedo;
uniform float uShininess;
uniform int   uMode;

void main() {
    vec3 lightColor = vec3(1.0, 0.93, 0.78);

    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightPos - vFragPos);
    vec3 V = normalize(uViewPos  - vFragPos);

    // Ambient는 직접광이 닿지 않는 면도 완전히 검게 보이지 않게 하는 단순 근사.
    vec3 ambient = 0.07 * uAlbedo;

    float diff   = max(dot(N, L), 0.0);
    vec3 diffuse = diff * lightColor * uAlbedo;

    float spec;
    if (uMode == 1) {
        // Phong: 반사 벡터 R과 view 방향 V의 각도로 highlight 계산.
        vec3 R = reflect(-L, N);
        spec = pow(max(dot(V, R), 0.0), uShininess);
    } else {
        // Blinn-Phong: light/view의 half vector H를 사용. 보통 더 안정적이고 빠르다.
        vec3 H = normalize(L + V);
        spec = pow(max(dot(N, H), 0.0), uShininess);
    }
    vec3 specular = spec * lightColor * 0.6;

    fragColor = vec4(ambient + diffuse + specular, 1.0);
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
    spdlog::info("OpenGL shader compiled: type=0x{:x}", static_cast<unsigned>(type));
    return s;
}

struct Material {
    glm::vec3 albedo;
    float shininess;
};

struct SceneObject {
    GLMesh    mesh;
    glm::mat4 model;
    Material  mat;
};

class LightingApp : public GLBaseApp {
    GLuint m_program = 0;

    GLint m_locModel = -1, m_locView = -1, m_locProj = -1;
    GLint m_locLightPos = -1, m_locViewPos = -1;
    GLint m_locAlbedo = -1, m_locShininess = -1, m_locMode = -1;

    SceneObject m_objects[4];
    SceneObject m_floor;

    glm::vec3 m_camPos;
    glm::quat m_camQuat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    float     m_lookDist = 9.0f;

    int       m_mode = 0;
    bool      m_dragging = false;
    glm::vec2 m_prevMouse{};

public:
    LightingApp() { m_title = "opengl-lighting [Blinn-Phong]"; }

private:
    glm::vec3 camForward() const { return -(m_camQuat * glm::vec3(0, 0, 1)); }

    glm::mat4 camTransform() const {
        return glm::translate(glm::mat4(1), m_camPos) * glm::mat4_cast(m_camQuat);
    }

    float projectToSphere(float r, float px, float py) const {
        // Arcball maps 2D mouse coordinates onto a virtual sphere.
        // Outside the sphere, a hyperbolic sheet keeps rotation smooth near edges.
        float d = std::sqrt(px * px + py * py);
        if (d < r * 0.70710678f) return std::sqrt(r * r - d * d);
        float t = r * 0.70710678f;
        return t * t / d;
    }

    glm::vec3 screenToSphere(glm::vec2 p) const {
        const float r = 0.5f;
        float nx =  p.x / m_width * 2.0f - 1.0f;
        float ny = (m_height - p.y - 1.0f) / m_height * 2.0f - 1.0f;
        return glm::normalize(glm::vec3(nx, ny, projectToSphere(r, nx, ny)));
    }

    void updateTitle() {
        const char* names[] = {"Blinn-Phong", "Phong"};
        m_title = std::string("opengl-lighting [") + names[m_mode] + "]";
        glfwSetWindowTitle(m_window, m_title.c_str());
    }

    void OnKey(int key, int action) override {
        if (action != GLFW_PRESS) return;
        if (key == GLFW_KEY_1) { m_mode = 0; updateTitle(); }
        if (key == GLFW_KEY_2) { m_mode = 1; updateTitle(); }
        if (key == GLFW_KEY_1 || key == GLFW_KEY_2)
            spdlog::info("Lighting model changed: {}", m_mode == 0 ? "Blinn-Phong" : "Phong");
    }

    void OnMouseButton(int button, int action, int) override {
        if (button != GLFW_MOUSE_BUTTON_LEFT) return;
        m_dragging = (action == GLFW_PRESS);
        if (m_dragging) {
            double x, y;
            glfwGetCursorPos(m_window, &x, &y);
            m_prevMouse = { float(x), float(y) };
        }
    }

    void OnMouseMove(double x, double y) override {
        if (!m_dragging) return;
        glm::vec2 curr{ float(x), float(y) };
        glm::vec3 p0 = screenToSphere(m_prevMouse);
        glm::vec3 p1 = screenToSphere(curr);
        m_prevMouse = curr;
        if (glm::length(p1 - p0) < 0.001f) return;

        float angle = std::acos(glm::clamp(glm::dot(p0, p1), -1.0f, 1.0f));
        glm::vec3 axis = m_camQuat * glm::normalize(glm::cross(p0, p1));
        glm::mat4 rot = glm::rotate(glm::mat4(1), -angle, axis);
        glm::mat4 tf = rot * camTransform();
        m_camPos = glm::vec3(tf[3]);
        m_camQuat = glm::normalize(glm::quat_cast(tf));
    }

    void OnScroll(double, double dy) override {
        float ratio = float(dy) * 0.02f;
        float zoom = 1.0f - std::fabs(ratio);
        m_lookDist = (ratio > 0.0f) ? m_lookDist * zoom : m_lookDist / zoom;
        m_lookDist = glm::clamp(m_lookDist, 2.0f, 25.0f);
        m_camPos = -camForward() * m_lookDist;
        spdlog::info("Camera zoom distance: {:.2f}", m_lookDist);
    }

    void OnInit() override {
        // Lighting needs correct front/back visibility, so enable depth testing.
        glEnable(GL_DEPTH_TEST);

        const glm::mat4 I(1);

        // Each object owns its mesh, model matrix, and material.
        // The shader is shared; per-object values are passed through uniforms before draw().
        m_floor = {
            GLMesh::from(plane()),
            glm::scale(I, {10, 10, 1}),
            { {0.20f, 0.20f, 0.22f}, 8.0f }
        };

        m_objects[0] = {
            GLMesh::from(sphere(0.72f)),
            glm::translate(I, { 1.8f,  0.6f, 0.72f }),
            { {0.90f, 0.32f, 0.22f}, 96.0f }
        };
        m_objects[1] = {
            GLMesh::from(torus(0.78f, 0.26f)),
            glm::translate(I, {-1.7f, -0.4f, 0.70f }),
            { {0.18f, 0.68f, 0.60f}, 28.0f }
        };
        m_objects[2] = {
            GLMesh::from(cube()),
            glm::translate(I, { 0.2f, -2.0f, 0.50f })
                * glm::rotate(I, glm::radians(28.0f), {0, 0, 1}),
            { {0.52f, 0.28f, 0.82f}, 10.0f }
        };
        m_objects[3] = {
            GLMesh::from(cylinder(0.55f, 1.4f)),
            glm::translate(I, {-0.6f,  1.7f, 0.30f }),
            { {0.88f, 0.64f, 0.10f}, 52.0f }
        };

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
        m_locLightPos = glGetUniformLocation(m_program, "uLightPos");
        m_locViewPos = glGetUniformLocation(m_program, "uViewPos");
        m_locAlbedo = glGetUniformLocation(m_program, "uAlbedo");
        m_locShininess = glGetUniformLocation(m_program, "uShininess");
        m_locMode = glGetUniformLocation(m_program, "uMode");

        // Build the initial camera from a lookAt matrix, then store it as position+quaternion
        // so mouse arcball rotation can update it incrementally.
        glm::vec3 eye = glm::normalize(glm::vec3(3.0f, -5.0f, 3.5f)) * m_lookDist;
        glm::mat4 camMat = glm::inverse(
            glm::lookAt(eye, glm::vec3(0, 0, 0.5f), glm::vec3(0, 0, 1))
        );
        m_camQuat = glm::normalize(glm::quat_cast(glm::mat3(camMat)));
        m_camPos = eye;
        spdlog::info("OpenGL lighting example initialized: objects={}, program={}, mode=Blinn-Phong",
                     std::size(m_objects) + 1, m_program);
    }

    void draw(const SceneObject& obj, const glm::mat4& view, const glm::mat4& proj) {
        // Uniforms are program-global state. Update them immediately before drawing
        // each object so the same mesh shader can render different transforms/materials.
        glUniformMatrix4fv(m_locModel, 1, GL_FALSE, glm::value_ptr(obj.model));
        glUniformMatrix4fv(m_locView, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(m_locProj, 1, GL_FALSE, glm::value_ptr(proj));
        glUniform3fv(m_locAlbedo, 1, glm::value_ptr(obj.mat.albedo));
        glUniform1f(m_locShininess, obj.mat.shininess);
        obj.mesh.draw();
    }

    void OnRender() override {
        glClearColor(0.04f, 0.04f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = glm::inverse(camTransform());
        float aspect = m_height > 0 ? float(m_width) / m_height : 1.0f;
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

        float a = float(glfwGetTime()) * 0.4f;
        glm::vec3 lightPos = { std::cos(a) * 6.0f, std::sin(a) * 6.0f, 5.5f };

        glUseProgram(m_program);
        glUniform3fv(m_locLightPos, 1, glm::value_ptr(lightPos));
        glUniform3fv(m_locViewPos,  1, glm::value_ptr(m_camPos));
        glUniform1i (m_locMode, m_mode);

        draw(m_floor, view, proj);
        for (const auto& obj : m_objects) draw(obj, view, proj);
    }

    void OnCleanup() override {
        m_floor.mesh.destroy();
        for (auto& obj : m_objects) obj.mesh.destroy();
        glDeleteProgram(m_program);
    }
};

int main() {
    LightingApp app;
    app.Run();
}
