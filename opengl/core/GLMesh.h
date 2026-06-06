#pragma once
#include <glad/gl.h>
#include <cstdint>
#include <vector>

struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

struct Mesh {
    std::vector<Vertex>   verts;
    std::vector<uint32_t> indices;
};

// 프리미티브 생성 (CPU 데이터)
Mesh quad    (float size = 0.5f);
Mesh cube    ();
Mesh sphere  (float radius = 0.5f);
Mesh cylinder(float radius = 0.5f, float height = 1.0f);
Mesh torus   (float radius = 0.6f, float tubeRadius = 0.2f);
Mesh plane   ();

// GPU 메시 핸들
struct GLMesh {
    GLuint   vao = 0, vbo = 0, ebo = 0;
    uint32_t count = 0;

    static GLMesh from(const Mesh& d);

    void draw()    const;
    void destroy();
};
