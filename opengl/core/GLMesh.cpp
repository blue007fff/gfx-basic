#include "core/GLMesh.h"

#include <spdlog/spdlog.h>

#pragma warning(push)
#pragma warning(disable : 4996 4305 4244)
#define PAR_SHAPES_IMPLEMENTATION
#include "par_shapes.h"
#pragma warning(pop)

// ─── 내부 헬퍼 ────────────────────────────────────────────────────────────────

namespace {

Mesh fromParShape(par_shapes_mesh* shape) {
    Mesh d;
    d.verts.resize(shape->npoints);
    d.indices.resize(static_cast<size_t>(shape->ntriangles) * 3);
    for (int i = 0; i < shape->npoints; ++i) {
        auto& v = d.verts[i];
        v.pos[0] = shape->points[i*3+0];
        v.pos[1] = shape->points[i*3+1];
        v.pos[2] = shape->points[i*3+2];
        if (shape->normals) {
            v.normal[0] = shape->normals[i*3+0];
            v.normal[1] = shape->normals[i*3+1];
            v.normal[2] = shape->normals[i*3+2];
        }
        if (shape->tcoords) {
            v.uv[0] = shape->tcoords[i*2+0];
            v.uv[1] = shape->tcoords[i*2+1];
        }
    }
    for (int i = 0; i < shape->ntriangles * 3; ++i)
        d.indices[i] = static_cast<uint32_t>(shape->triangles[i]);
    return d;
}

} // namespace

GLMesh GLMesh::from(const Mesh& d) {
    GLMesh m;
    m.count = static_cast<uint32_t>(d.indices.size());

    // VAO는 "정점 버퍼 자체"가 아니라 attribute binding 상태를 기억한다.
    // VBO/EBO를 바인딩한 뒤 attribute pointer를 지정하면 그 연결이 VAO에 저장된다.
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);

    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(d.verts.size() * sizeof(Vertex)),
        d.verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(d.indices.size() * sizeof(uint32_t)),
        d.indices.data(), GL_STATIC_DRAW);

    constexpr GLsizei stride = sizeof(Vertex);
    // Shader location과 C++ Vertex 구조체 layout을 맞춘다.
    // location 0 = position(float3), 1 = normal(float3), 2 = uv(float2).
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    spdlog::info("GL mesh uploaded: vertices={}, indices={}", d.verts.size(), d.indices.size());
    return m;
}

// ─── 프리미티브 생성 ──────────────────────────────────────────────────────────

Mesh quad(float size) {
    Mesh d;
    d.verts = {
        {{-size,  size, 0}, {0,0,1}, {0,1}},
        {{ size,  size, 0}, {0,0,1}, {1,1}},
        {{ size, -size, 0}, {0,0,1}, {1,0}},
        {{-size, -size, 0}, {0,0,1}, {0,0}},
    };
    d.indices = {0,1,2, 0,2,3};
    return d;
}

Mesh cube() {
    // par_shapes에는 box primitive가 없어서 plane 6개를 회전/병합해 cube를 만든다.
    // 각 면이 독립 정점을 가지므로 normal/uv가 면 단위로 깨끗하게 유지된다.
    float y[]{0,1,0}, xa[]{1,0,0}, za[]{0,0,1};
    par_shapes_mesh* pz = par_shapes_create_plane(4, 4);
    par_shapes_translate(pz, -0.5f, -0.5f, 0.5f);
    par_shapes_mesh* mz = par_shapes_clone(pz, nullptr); par_shapes_rotate(mz, float(PAR_PI), y);
    par_shapes_mesh* my = par_shapes_clone(pz, nullptr); par_shapes_rotate(my, float(PAR_PI*.5), xa);
    par_shapes_mesh* px = par_shapes_clone(my, nullptr); par_shapes_rotate(px, float(PAR_PI*.5), za);
    par_shapes_mesh* py = par_shapes_clone(px, nullptr); par_shapes_rotate(py, float(PAR_PI*.5), za);
    par_shapes_mesh* mx = par_shapes_clone(py, nullptr); par_shapes_rotate(mx, float(PAR_PI*.5), za);
    par_shapes_merge_and_free(pz, mz);
    par_shapes_merge_and_free(pz, my);
    par_shapes_merge_and_free(pz, px);
    par_shapes_merge_and_free(pz, py);
    par_shapes_merge_and_free(pz, mx);
    auto d = fromParShape(pz);
    par_shapes_free_mesh(pz);
    return d;
}

Mesh sphere(float radius) {
    // Parametric sphere는 세그먼트가 많을수록 매끄럽지만 vertex/index 수도 증가한다.
    par_shapes_mesh* shape = par_shapes_create_parametric_sphere(36, 18);
    par_shapes_scale(shape, radius, radius, radius);
    auto d = fromParShape(shape);
    for (auto& v : d.verts) { std::swap(v.uv[0], v.uv[1]); v.uv[1] = 1.0f - v.uv[1]; }
    par_shapes_free_mesh(shape);
    return d;
}

Mesh cylinder(float radius, float height) {
    // par_shapes cylinder는 옆면만 만들기 때문에 얇게 눌린 hemisphere를 cap으로 붙인다.
    float xa[]{1,0,0}, ya[]{0,1,0}, za[]{0,0,1};
    par_shapes_mesh* shape = par_shapes_create_cylinder(36, 4);
    for (int i = 0; i < shape->npoints; ++i) {
        std::swap(shape->tcoords[i*2], shape->tcoords[i*2+1]);
        shape->tcoords[i*2] = 1.0f - shape->tcoords[i*2];
    }
    par_shapes_scale(shape, radius, radius, height);
    par_shapes_mesh* cap = par_shapes_create_hemisphere(3, 36);
    par_shapes_rotate(cap, float(PAR_PI*.5), xa);
    par_shapes_rotate(cap, -float(PAR_PI*.5), za);
    par_shapes_scale(cap, radius, radius, 0.0f);
    par_shapes_mesh* bot = par_shapes_clone(cap, nullptr);
    par_shapes_rotate(bot, float(PAR_PI), ya);
    par_shapes_merge_and_free(shape, bot);
    par_shapes_translate(cap, 0, 0, height);
    par_shapes_merge_and_free(shape, cap);
    auto d = fromParShape(shape);
    par_shapes_free_mesh(shape);
    return d;
}

Mesh torus(float radius, float tubeRadius) {
    par_shapes_mesh* shape = par_shapes_create_torus(36, 36, tubeRadius / radius);
    par_shapes_scale(shape, radius, radius, radius);
    auto d = fromParShape(shape);
    par_shapes_free_mesh(shape);
    return d;
}

Mesh plane() {
    par_shapes_mesh* shape = par_shapes_create_plane(1, 1);
    par_shapes_translate(shape, -0.5f, -0.5f, 0.0f);
    auto d = fromParShape(shape);
    par_shapes_free_mesh(shape);
    return d;
}

// ─── draw / destroy ───────────────────────────────────────────────────────────

void GLMesh::draw() const {
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, nullptr);
}

void GLMesh::destroy() {
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    vao = vbo = ebo = count = 0;
}
