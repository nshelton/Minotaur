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

    // Add a colored line segment in plot mm page space
    void addLine(Vec2 a, Vec2 b, Color c);

    void draw(const Mat3 &t);

private:
    struct GLVertex { float x, y, r, g, b, a; };

    GLuint m_program{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLuint m_uProjMat{0};

    float m_lineWidth{1.0f};

    std::vector<GLVertex> m_vertices;

    static GLuint compileShader(GLenum type, const char* src);
    static GLuint linkProgram(GLuint vs, GLuint fs);
};

