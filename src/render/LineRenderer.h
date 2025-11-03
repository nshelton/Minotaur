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
    void setPointRadiusMm(float r) { m_pointRadiusMm = r; }
    void setPointDiameterPx(float d) { m_pointDiameterPx = d; }
    float lineWidth() const { return m_lineWidth; }

    // Add a colored line segment in plot mm page space
    void addLine(Vec2 a, Vec2 b, Color c);
    // Add a point to be rendered as a filled circle (GL_POINTS sprite)
    void addPoint(Vec2 p, Color c);

    int totalVertices() const { return static_cast<int>(m_vertices.size()); }

    void draw(const Mat3 &t);

private:
    struct GLVertex { float x, y, r, g, b, a; };

    GLuint m_program{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLuint m_uProjMat{0};
    GLuint m_uPointSizePx{0};
    GLuint m_uIsPointPass{0};

    float m_lineWidth{1.0f};
    float m_pointRadiusMm{1.5f};
    float m_pointDiameterPx{0.0f};

    std::vector<GLVertex> m_vertices; // line vertices (as pairs)
    std::vector<GLVertex> m_points;   // point vertices (single positions)

    static GLuint compileShader(GLenum type, const char* src);
    static GLuint linkProgram(GLuint vs, GLuint fs);
};

