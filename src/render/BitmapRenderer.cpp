#include "BitmapRenderer.h"

#include <iostream>

GLuint BitmapRenderer::compileShader(GLenum type, const char *src)
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

GLuint BitmapRenderer::linkProgram(GLuint vs, GLuint fs)
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

bool BitmapRenderer::init()
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

    const char *fsGraySrc = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;
void main(){
    float g = texture(uTex, vUV).r;
    FragColor = vec4(g, g, g, 1.0);
}
)";

    const char *fsRGBSrc = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;
void main(){
    vec3 rgb = texture(uTex, vUV).rgb;
    FragColor = vec4(rgb, 1.0);
}
)";

    // Create grayscale shader program
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    if (!vs)
        return false;
    GLuint fsGray = compileShader(GL_FRAGMENT_SHADER, fsGraySrc);
    if (!fsGray)
    {
        glDeleteShader(vs);
        return false;
    }
    m_programGray = linkProgram(vs, fsGray);
    glDeleteShader(fsGray);
    if (!m_programGray)
    {
        glDeleteShader(vs);
        return false;
    }

    m_uProjMatGray = glGetUniformLocation(m_programGray, "uProjectMat");
    m_uSamplerGray = glGetUniformLocation(m_programGray, "uTex");

    // Create RGB shader program (reuse vertex shader)
    GLuint fsRGB = compileShader(GL_FRAGMENT_SHADER, fsRGBSrc);
    if (!fsRGB)
    {
        glDeleteShader(vs);
        glDeleteProgram(m_programGray);
        m_programGray = 0;
        return false;
    }
    m_programRGB = linkProgram(vs, fsRGB);
    glDeleteShader(vs);
    glDeleteShader(fsRGB);
    if (!m_programRGB)
    {
        glDeleteProgram(m_programGray);
        m_programGray = 0;
        return false;
    }

    m_uProjMatRGB = glGetUniformLocation(m_programRGB, "uProjectMat");
    m_uSamplerRGB = glGetUniformLocation(m_programRGB, "uTex");

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

void BitmapRenderer::shutdown()
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
    if (m_programGray)
        glDeleteProgram(m_programGray);
    if (m_programRGB)
        glDeleteProgram(m_programRGB);
    m_vao = 0;
    m_vbo = 0;
    m_programGray = 0;
    m_programRGB = 0;
}

void BitmapRenderer::clear()
{
    m_quads.clear();
}

void BitmapRenderer::ensureTexture(int entityId, const Bitmap &bm)
{
    auto it = m_textures.find(entityId);
    bool needCreate = (it == m_textures.end()) || (it->second.w != bm.width_px) || (it->second.h != bm.height_px) || it->second.isColor;
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

        m_textures[entityId] = TexInfo{tex, static_cast<size_t>(bm.width_px), static_cast<size_t>(bm.height_px), false};
    }

    // Always upload latest pixel data in case content changed without size change
    GLuint tex = m_textures[entityId].tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, bm.width_px, bm.height_px, GL_RED, GL_UNSIGNED_BYTE, bm.pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void BitmapRenderer::ensureColorTexture(int entityId, const ColorImage &ci)
{
    auto it = m_textures.find(entityId);
    bool needCreate = (it == m_textures.end()) || (it->second.w != ci.width_px) || (it->second.h != ci.height_px) || !it->second.isColor;
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, static_cast<int>(ci.width_px), static_cast<int>(ci.height_px), 0, GL_RGB, GL_UNSIGNED_BYTE, ci.pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        m_textures[entityId] = TexInfo{tex, ci.width_px, ci.height_px, true};
    }

    // Always upload latest pixel data in case content changed without size change
    GLuint tex = m_textures[entityId].tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<int>(ci.width_px), static_cast<int>(ci.height_px), GL_RGB, GL_UNSIGNED_BYTE, ci.pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void BitmapRenderer::addBitmap(int entityId, const Bitmap &bm, const Mat3 &localToPage)
{
    ensureTexture(entityId, bm);
    BoundingBox bb = bm.aabb();
    Quad q;
    q.pMin = localToPage.apply(bb.min);
    q.pMax = localToPage.apply(bb.max);
    q.texture = m_textures[entityId].tex;
    q.isColor = false;
    m_quads.push_back(q);
}

void BitmapRenderer::addColorImage(int entityId, const ColorImage &ci, const Mat3 &localToPage)
{
    ensureColorTexture(entityId, ci);
    BoundingBox bb = ci.aabb();
    Quad q;
    q.pMin = localToPage.apply(bb.min);
    q.pMax = localToPage.apply(bb.max);
    q.texture = m_textures[entityId].tex;
    q.isColor = true;
    m_quads.push_back(q);
}

void BitmapRenderer::draw(const Mat3 &mm_to_ndc)
{
    if (m_quads.empty())
        return;

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    for (const auto &q : m_quads)
    {
        // Select appropriate shader program based on whether this is a color or grayscale image
        GLuint program = q.isColor ? m_programRGB : m_programGray;
        GLint uProjMat = q.isColor ? m_uProjMatRGB : m_uProjMatGray;
        GLint uSampler = q.isColor ? m_uSamplerRGB : m_uSamplerGray;

        glUseProgram(program);
        glUniformMatrix3fv(uProjMat, 1, GL_FALSE, mm_to_ndc.m);
        glUniform1i(uSampler, 0);

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


