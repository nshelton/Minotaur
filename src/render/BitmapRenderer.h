#pragma once

#include <glad/glad.h>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "core/Core.h"

class BitmapRenderer
{
public:
    bool init();
    void shutdown();

    void clear();

    // Ensure texture exists for bitmap of an entity, then queue a quad to draw.
    // version identifies the pixel content; the texture is re-uploaded only when
    // it differs from the last uploaded version (or on first create / resize).
    void addBitmap(int entityId, const Bitmap &bm, const Mat3 &localToPage, uint64_t version);
    void addColorImage(int entityId, const ColorImage &ci, const Mat3 &localToPage, uint64_t version);

    void draw(const Mat3 &mm_to_ndc);

private:
    struct Quad
    {
        Vec2 pMin; // page mm
        Vec2 pMax; // page mm
        GLuint texture{0};
        bool isColor{false};
    };

    struct TexInfo
    {
        GLuint tex{0};
        size_t w{0};
        size_t h{0};
        bool isColor{false};
        uint64_t uploadedVersion{0};
    };

    GLuint m_programGray{0};
    GLuint m_programRGB{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLint m_uProjMatGray{0};
    GLint m_uSamplerGray{0};
    GLint m_uProjMatRGB{0};
    GLint m_uSamplerRGB{0};

    std::unordered_map<int, TexInfo> m_textures; // by entity id
    std::vector<Quad> m_quads;

    static GLuint compileShader(GLenum type, const char *src);
    static GLuint linkProgram(GLuint vs, GLuint fs);
    void ensureTexture(int entityId, const Bitmap &bm, uint64_t version);
    void ensureColorTexture(int entityId, const ColorImage &ci, uint64_t version);
};


