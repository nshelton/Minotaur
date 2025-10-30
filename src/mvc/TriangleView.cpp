#include "TriangleView.h"

#include <vector>
#include <iostream>

GLuint TriangleView::compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(logLen));
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        std::cerr << "Shader compilation failed: " << log.data() << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint TriangleView::linkProgram(GLuint vert, GLuint frag) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLint logLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(logLen));
        glGetProgramInfoLog(program, logLen, nullptr, log.data());
        std::cerr << "Program link failed: " << log.data() << std::endl;
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

bool TriangleView::init() {
    const char* vertexShaderSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
uniform vec2 uTranslate;
uniform float uScale;
out vec3 vColor;
void main() {
    vColor = aColor;
    vec2 p = (aPos.xy + uTranslate) * uScale;
    gl_Position = vec4(p, aPos.z, 1.0);
}
)";

    const char* fragmentShaderSrc = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

    GLuint vert = compileShader(GL_VERTEX_SHADER, vertexShaderSrc);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSrc);
    if (vert == 0 || frag == 0) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return false;
    }
    m_program = linkProgram(vert, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);
    if (m_program == 0) return false;

    m_uTranslate = glGetUniformLocation(m_program, "uTranslate");
    m_uScale = glGetUniformLocation(m_program, "uScale");

    float vertices[] = {
        // positions         // colors
         0.0f,  0.5f, 0.0f,  1.0f, 0.3f, 0.3f,
        -0.5f, -0.5f, 0.0f,  0.3f, 1.0f, 0.3f,
         0.5f, -0.5f, 0.0f,  0.3f, 0.3f, 1.0f
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return true;
}

void TriangleView::render() {
    glUseProgram(m_program);
    if (m_uTranslate >= 0) glUniform2f(m_uTranslate, m_tx, m_ty);
    if (m_uScale >= 0) glUniform1f(m_uScale, m_scale);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void TriangleView::shutdown() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_program) glDeleteProgram(m_program);
    m_vao = 0; m_vbo = 0; m_program = 0;
}

void TriangleView::setTransform(float tx, float ty, float scale) {
    m_tx = tx;
    m_ty = ty;
    m_scale = scale;
}
