#pragma once

#include "core/Core.h"
#include "render/LineRenderer.h"
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

private:
    LineRenderer m_lines{};

    void renderPage(const Camera &camera, const PageModel &page);
};
