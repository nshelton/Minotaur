#pragma once

#include <glad/glad.h>

class TriangleView {
public:
    bool init();
    void render();
    void shutdown();

private:
    GLuint m_program{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};

    static GLuint compileShader(GLenum type, const char* source);
    static GLuint linkProgram(GLuint vert, GLuint frag);
};

