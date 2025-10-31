#pragma once

#include <glad/glad.h>
#include <vector>
#include "core/core.h"

class LineRenderer {
public:
    bool init();
    void shutdown();

    void clear();
    void setLineWidth(float w) { m_lineWidth = w; }
    void setTransform(Vec2 pos, float scale);

    // Add a colored line segment in NDC space (before transform)
    void addLine(Vec2 a, Vec2 b, Color c);

    void draw();

private:
    struct GLVertex { float x, y, r, g, b, a; };

    GLuint m_program{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLint m_uTranslate{-1};
    GLint m_uScale{-1};

    Vec2 m_pos {0, 0};
    float m_scale{1.0f};
    float m_lineWidth{1.0f};

    std::vector<GLVertex> m_vertices;

    static GLuint compileShader(GLenum type, const char* src);
    static GLuint linkProgram(GLuint vs, GLuint fs);
};

