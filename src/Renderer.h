#pragma once

#include "core/Core.h"
#include "render/LineRenderer.h"
#include "render/BitmapRenderer.h"
#include "Interaction.h"
#include "Page.h"
#include "Camera.h"

#include <glog/logging.h>


#define HANDLE_RENDER_RADIUS_MM 3.0f
class Renderer
{
public:
    Renderer();

    void setSize(int width, int height)
    {
        LOG(INFO) << "GL size set to " << width << "x" << height;
        glViewport(0, 0, width, height);
    }

    void render(const Camera &camera, const PageModel &page, const InteractionState &uiState);

    void setLineWidth(float w) { m_lines.setLineWidth(w); }
    void setNodeDiameterPx(float d) { m_nodeDiameterPx = d; m_lines.setPointDiameterPx(d); }
    float lineWidth() const { return m_lines.lineWidth(); }
    float nodeDiameterPx() const { return m_nodeDiameterPx; }

    void shutdown();

    int totalVertices() const { return static_cast<int>(m_lines.totalVertices()); }

private:
    LineRenderer m_lines{};
    BitmapRenderer m_images{};
    float m_nodeDiameterPx{8.0f};

    void renderPage(const Camera &camera, const PageModel &page);
    void drawRect(const Vec2 &min, const Vec2 &max, const Color &col);
    void drawHandle(const Vec2 &center, float sizeMm, const Color &col);
    void drawCircle(const Vec2 &center, float radiusMm, const Color &col);
};
