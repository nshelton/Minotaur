#include "ImageRenderer.h"

#include <iostream>

GLuint ImageRenderer::compileShader(GLenum type, const char *src)
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
        std::cerr << "ImageRenderer shader compile failed: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint ImageRenderer::linkProgram(GLuint vs, GLuint fs)
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
        std::cerr << "ImageRenderer link failed: " << log << std::endl;
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool ImageRenderer::init()
{
    const char *vsSrc = R"(
#version 330 core
layout(location=0) in vec2 aPos;   // mm page space
layout(location=1) in vec2 aUV;
uniform mat3 uProjectMat;
out vec2 vUV;
void main(){
    vec3 ndc = uProjectMat * vec3(aPos, 1.0);
    gl_Position = vec4(ndc.xy, 0.0, 1.0);
    vUV = aUV;
}
)";

    const char *fsSrc = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;
void main(){
    float g = texture(uTex, vUV).r;
    FragColor = vec4(g, g, g, 1.0);
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
    m_uSampler = glGetUniformLocation(m_program, "uTex");

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void *)(2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return true;
}

void ImageRenderer::shutdown()
{
    for (auto &kv : m_textures)
    {
        if (kv.second.tex)
            glDeleteTextures(1, &kv.second.tex);
    }
    m_textures.clear();
    m_quads.clear();
    if (m_vao)
        glDeleteVertexArrays(1, &m_vao);
    if (m_vbo)
        glDeleteBuffers(1, &m_vbo);
    if (m_program)
        glDeleteProgram(m_program);
    m_vao = 0;
    m_vbo = 0;
    m_program = 0;
}

void ImageRenderer::clear()
{
    m_quads.clear();
}

void ImageRenderer::ensureTexture(int entityId, const Bitmap &bm)
{
    auto it = m_textures.find(entityId);
    bool needCreate = (it == m_textures.end()) || (it->second.w != bm.width_px) || (it->second.h != bm.height_px);
    if (needCreate)
    {
        if (it != m_textures.end() && it->second.tex)
        {
            glDeleteTextures(1, &it->second.tex);
            m_textures.erase(it);
        }
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, bm.width_px, bm.height_px, 0, GL_RED, GL_UNSIGNED_BYTE, bm.pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        m_textures[entityId] = TexInfo{tex, bm.width_px, bm.height_px};
    }
}

void ImageRenderer::addBitmap(int entityId, const Bitmap &bm, const Mat3 &localToPage)
{
    ensureTexture(entityId, bm);
    BoundingBox bb = bm.aabb();
    Quad q;
    q.pMin = localToPage.apply(bb.min);
    q.pMax = localToPage.apply(bb.max);
    q.texture = m_textures[entityId].tex;
    m_quads.push_back(q);
}

void ImageRenderer::draw(const Mat3 &mm_to_ndc)
{
    if (m_quads.empty())
        return;

    glUseProgram(m_program);
    glUniformMatrix3fv(m_uProjMat, 1, GL_FALSE, mm_to_ndc.m);
    glUniform1i(m_uSampler, 0);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    for (const auto &q : m_quads)
    {
        // 2 triangles (6 verts): (min,min)->(max,min)->(max,max) and (min,min)->(max,max)->(min,max)
        float verts[6 * 4] = {
            q.pMin.x, q.pMin.y, 0.0f, 0.0f,
            q.pMax.x, q.pMin.y, 1.0f, 0.0f,
            q.pMax.x, q.pMax.y, 1.0f, 1.0f,

            q.pMin.x, q.pMin.y, 0.0f, 0.0f,
            q.pMax.x, q.pMax.y, 1.0f, 1.0f,
            q.pMin.x, q.pMax.y, 0.0f, 1.0f,
        };
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, q.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}


