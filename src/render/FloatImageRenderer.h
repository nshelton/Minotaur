#pragma once

#include <glad/glad.h>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "core/Core.h"

class FloatImageRenderer
{
public:
    bool init();
    void shutdown();

    void clear();

    // Ensure texture exists for float image of an entity, then queue a quad to draw.
    // version identifies the pixel content; the texture is re-uploaded only when
    // it differs from the last uploaded version (or on first create / resize).
    void addFloatImage(int entityId, const FloatImage &img, const Mat3 &localToPage, uint64_t version);

    void draw(const Mat3 &mm_to_ndc);

private:
    struct Quad
    {
        Vec2 pMin; // page mm
        Vec2 pMax; // page mm
        GLuint texture{0};
        float minVal{0.0f};
        float maxVal{1.0f};
    };

    struct TexInfo
    {
        GLuint tex{0};
        size_t w{0};
        size_t h{0};
        uint64_t uploadedVersion{0};
    };

    GLuint m_program{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLint m_uProjMat{0};
    GLint m_uSampler{0};
    GLint m_uMin{0};
    GLint m_uInvRange{0};

    std::unordered_map<int, TexInfo> m_textures; // by entity id
    std::vector<Quad> m_quads;

    static GLuint compileShader(GLenum type, const char *src);
    static GLuint linkProgram(GLuint vs, GLuint fs);
    void ensureTexture(int entityId, const FloatImage &img, uint64_t version);
};



