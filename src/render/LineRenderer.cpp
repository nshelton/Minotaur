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
uniform mat3 uProjectMat;
uniform float uPointSizePx;
out vec4 vColor;
void main(){
    vec3 worldPos = uProjectMat * vec3(aPos, 1.0);
    gl_Position = vec4(worldPos.xy, 0.0, 1.0);
    gl_PointSize = uPointSizePx;
    vColor = aColor;
}
)";

    const char *fsSrc = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
uniform int uIsPointPass;
void main(){
    if (uIsPointPass == 1) {
        // circular point sprite mask
        vec2 d = gl_PointCoord - vec2(0.5);
        if (dot(d, d) > 0.25) discard;
    }
    FragColor = vColor;
}
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

    m_uProjMat = glGetUniformLocation(m_program, "uProjectMat");
    m_uPointSizePx = glGetUniformLocation(m_program, "uPointSizePx");
    m_uIsPointPass = glGetUniformLocation(m_program, "uIsPointPass");

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

/// @brief Add a line to the render list in mm page space
/// @param p1 point in mm page space
/// @param p2 point in mm page spaces
/// @param c color
void LineRenderer::addLine(Vec2 p1, Vec2 p2, Color c)
{
    m_vertices.push_back(GLVertex{p1.x, p1.y, c.r, c.g, c.b, c.a});
    m_vertices.push_back(GLVertex{p2.x, p2.y, c.r, c.g, c.b, c.a});
}

void LineRenderer::addPoint(Vec2 p, Color c)
{
    m_points.push_back(GLVertex{p.x, p.y, c.r, c.g, c.b, c.a});
}

/// @brief Draw the lines in the specified coordinate space. Lines are stored in mm page space,
/// this controls how to render it to the screen.
/// @param mm_to_ndc
void LineRenderer::draw( const Mat3 &mm_to_ndc)
{
    glUseProgram(m_program);
    Vec2 pos = mm_to_ndc.translation();
    glUniformMatrix3fv(m_uProjMat, 1, GL_FALSE, mm_to_ndc.m);
    glUniform1i(m_uIsPointPass, 0);

    // Draw lines
    if (!m_vertices.empty())
    {
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(m_vertices.size() * sizeof(GLVertex)),
            m_vertices.data(), GL_DYNAMIC_DRAW);

        if (m_lineWidth > 0.0f)
            glLineWidth(m_lineWidth);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_vertices.size()));
    }

    // Draw points as filled circles using point sprites
    if (!m_points.empty())
    {
        // compute pixel size: use explicit pixel diameter if set; otherwise derive from mm
        float pointDiameterPx = m_pointDiameterPx;
        if (pointDiameterPx <= 0.0f)
        {
            // NDC per mm is in mm_to_ndc scale (m[0] and m[4])
            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            float vpW = static_cast<float>(vp[2]);
            float vpH = static_cast<float>(vp[3]);
            float ndcPerMmX = std::abs(mm_to_ndc.m[0]);
            float ndcPerMmY = std::abs(mm_to_ndc.m[4]);
            float pxPerMmX = ndcPerMmX * (vpW * 0.5f);
            float pxPerMmY = ndcPerMmY * (vpH * 0.5f);
            float pxPerMm = (pxPerMmX + pxPerMmY) * 0.5f;
            pointDiameterPx = std::max(1.0f, 2.0f * m_pointRadiusMm * pxPerMm);
        }

        glEnable(GL_PROGRAM_POINT_SIZE);
        glUniform1f(m_uPointSizePx, pointDiameterPx);
        glUniform1i(m_uIsPointPass, 1);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(m_points.size() * sizeof(GLVertex)),
            m_points.data(), GL_DYNAMIC_DRAW);

        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(m_points.size()));

        glUniform1i(m_uIsPointPass, 0);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // clear points for next frame
    m_points.clear();
}
