#pragma once

#include "core/Core.h"
#include "render/LineRenderer.h"
#include "Interaction.h"
#include "Page.h"
#include "Camera.h"

class Renderer
{
public:
    void init() { m_lines.init(); }

    void render(const Camera &camera, const PageModel &page, const InteractionState &uiState);

    void setLineWidth(float w) { m_lines.setLineWidth(w); }

    void setWindowSize(int width, int height)
    {
        m_winW = width;
        m_winH = height;
    }

private:
    LineRenderer m_lines{};

    int m_winW{0};
    int m_winH{0};

    void renderPage(const Camera &camera, const PageModel &page, const Color &col);
};
