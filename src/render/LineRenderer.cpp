#include "LineRenderer.h"

#include <iostream>

GLuint LineRenderer::compileShader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::string log;
        log.resize(static_cast<size_t>(len));
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::cerr << "LineRenderer shader compile failed: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint LineRenderer::linkProgram(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::string log;
        log.resize(static_cast<size_t>(len));
        glGetProgramInfoLog(p, len, nullptr, log.data());
        std::cerr << "LineRenderer link failed: " << log << std::endl;
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool LineRenderer::init()
{
    const char *vsSrc = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aColor;
uniform vec2 uTranslate;
uniform float uScale;
out vec4 vColor;
void main(){
    vec2 p = (aPos + uTranslate) * uScale;
    vColor = aColor;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

    const char *fsSrc = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main(){ FragColor = vColor; }
)";

    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    if (!vs)
        return false;
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!fs)
    {
        glDeleteShader(vs);
        return false;
    }
    m_program = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!m_program)
        return false;

    m_uTranslate = glGetUniformLocation(m_program, "uTranslate");
    m_uScale = glGetUniformLocation(m_program, "uScale");

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)(2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return true;
}

void LineRenderer::shutdown()
{
    if (m_vao)
        glDeleteVertexArrays(1, &m_vao);
    if (m_vbo)
        glDeleteBuffers(1, &m_vbo);
    if (m_program)
        glDeleteProgram(m_program);
    m_vao = 0;
    m_vbo = 0;
    m_program = 0;
    m_vertices.clear();
}

void LineRenderer::clear() { m_vertices.clear(); }

void LineRenderer::setTransform(Transform2D t)
{
    m_transform = t;
}

void LineRenderer::addLine(Vec2 p1, Vec2 p2, Color c)
{
    m_vertices.push_back(GLVertex{p1.x, p1.y, c.r, c.g, c.b, c.a});
    m_vertices.push_back(GLVertex{p2.x, p2.y, c.r, c.g, c.b, c.a});
}

void LineRenderer::draw()
{
    if (m_vertices.empty())
        return;

    glUseProgram(m_program);
    if (m_uTranslate >= 0)
        glUniform2f(m_uTranslate, m_transform.pos.x, m_transform.pos.y);
    if (m_uScale >= 0)
        glUniform1f(m_uScale, m_transform.scale);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vertices.size() * sizeof(GLVertex)), m_vertices.data(), GL_DYNAMIC_DRAW);

    if (m_lineWidth > 0.0f)
        glLineWidth(m_lineWidth);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_vertices.size()));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}
