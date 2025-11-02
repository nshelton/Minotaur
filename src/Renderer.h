#pragma once

#include "core/Core.h"
#include "render/LineRenderer.h"
#include "render/ImageRenderer.h"
#include "Interaction.h"
#include "Page.h"
#include "Camera.h"

#include <glog/logging.h>

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

    void shutdown();

    int totalVertices() const { return static_cast<int>(m_lines.totalVertices()); }

private:
    LineRenderer m_lines{};
    ImageRenderer m_images{};

    void renderPage(const Camera &camera, const PageModel &page);
    void drawRect(const Vec2 &min, const Vec2 &max, const Color &col);
    void drawHandle(const Vec2 &center, float sizeMm, const Color &col);
};
