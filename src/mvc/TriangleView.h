#pragma once

#include <glad/glad.h>

class TriangleView {
public:
    bool init();
    void render();
    void shutdown();
    void setTransform(float tx, float ty, float scale);

private:
    GLuint m_program{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLint m_uTranslate{-1};
    GLint m_uScale{-1};
    float m_tx{0.0f};
    float m_ty{0.0f};
    float m_scale{1.0f};

    static GLuint compileShader(GLenum type, const char* source);
    static GLuint linkProgram(GLuint vert, GLuint frag);
};
