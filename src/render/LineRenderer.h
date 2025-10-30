#pragma once

#include <glad/glad.h>
#include <vector>

class LineRenderer {
public:
    bool init();
    void shutdown();

    void clear();
    void setLineWidth(float w) { m_lineWidth = w; }
    void setTransform(float tx, float ty, float scale);

    // Add a colored line segment in NDC space (before transform)
    void addLine(float x1, float y1, float x2, float y2,
                 float r, float g, float b, float a = 1.0f);

    void draw();

private:
    struct Vertex { float x, y, r, g, b, a; };

    GLuint m_program{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLint m_uTranslate{-1};
    GLint m_uScale{-1};

    float m_tx{0.0f};
    float m_ty{0.0f};
    float m_scale{1.0f};
    float m_lineWidth{1.0f};

    std::vector<Vertex> m_vertices;

    static GLuint compileShader(GLenum type, const char* src);
    static GLuint linkProgram(GLuint vs, GLuint fs);
};

