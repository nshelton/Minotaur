#include "FloatImageRenderer.h"

#include <iostream>

GLuint FloatImageRenderer::compileShader(GLenum type, const char *src)
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
        std::cerr << "FloatImageRenderer shader compile failed: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint FloatImageRenderer::linkProgram(GLuint vs, GLuint fs)
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
        std::cerr << "FloatImageRenderer link failed: " << log << std::endl;
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool FloatImageRenderer::init()
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
uniform float uMin;
uniform float uInvRange;
out vec4 FragColor;
vec3 TurboColormap(float x) {
    const vec4 kRedVec4 = vec4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
    const vec4 kGreenVec4 = vec4(0.09140261, 2.19418839, 4.84296658, -14.18503333);
    const vec4 kBlueVec4 = vec4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
    const vec2 kRedVec2 = vec2(-152.94239396, 59.28637943);
    const vec2 kGreenVec2 = vec2(4.27729857, 2.82956604);
    const vec2 kBlueVec2 = vec2(-89.90310912, 27.34824973);

    x = clamp(x, 0.0, 1.0);
    vec4 v4 = vec4(1.0, x, x * x, x * x * x);
    vec2 v2 = v4.zw * v4.z;
    return vec3(
        dot(v4, kRedVec4)   + dot(v2, kRedVec2),
        dot(v4, kGreenVec4) + dot(v2, kGreenVec2),
        dot(v4, kBlueVec4)  + dot(v2, kBlueVec2)
    );
}
void main(){
    float g = texture(uTex, vUV).r;
    float x = (g - uMin) * uInvRange;
    vec3 col = TurboColormap(x);
    FragColor = vec4(col, 1.0);
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
    m_uMin = glGetUniformLocation(m_program, "uMin");
    m_uInvRange = glGetUniformLocation(m_program, "uInvRange");

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

void FloatImageRenderer::shutdown()
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

void FloatImageRenderer::clear()
{
    m_quads.clear();
}

void FloatImageRenderer::ensureTexture(int entityId, const FloatImage &img)
{
    auto it = m_textures.find(entityId);
    bool needCreate = (it == m_textures.end()) || (it->second.w != img.width_px) || (it->second.h != img.height_px);
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, (GLsizei)img.width_px, (GLsizei)img.height_px, 0, GL_RED, GL_FLOAT, img.pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        m_textures[entityId] = TexInfo{tex, img.width_px, img.height_px};
    }

    // Always upload latest pixel data in case content changed without size change
    GLuint tex = m_textures[entityId].tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)img.width_px, (GLsizei)img.height_px, GL_RED, GL_FLOAT, img.pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void FloatImageRenderer::addFloatImage(int entityId, const FloatImage &img, const Mat3 &localToPage)
{
    ensureTexture(entityId, img);
    BoundingBox bb = img.aabb();
    Quad q;
    q.pMin = localToPage.apply(bb.min);
    q.pMax = localToPage.apply(bb.max);
    q.texture = m_textures[entityId].tex;
    q.minVal = img.minValue;
    q.maxVal = img.maxValue;
    m_quads.push_back(q);
}

void FloatImageRenderer::draw(const Mat3 &mm_to_ndc)
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
        float invRange = (q.maxVal > q.minVal) ? (1.0f / (q.maxVal - q.minVal)) : 0.0f;
        glUniform1f(m_uMin, q.minVal);
        glUniform1f(m_uInvRange, invRange);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}



