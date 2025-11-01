#include "Renderer.h"

#include <algorithm>


Renderer::Renderer()
{
    m_lines.init();
}

void Renderer::render(const Camera &camera, const PageModel &page, const InteractionState &uiState)
{
   // draw page extent and grid
   renderPage(camera, page, Color(0.8f, 1.0f, 0.8f, 1.0f));

   for (const auto &ps : page.entities)
   {
      for (const auto &path : ps.paths)
      {
         // add lines from path here
      }
   }

   //  Vec2 mouse_mm = model.mouse_page_mm;
   //  Vec2 s_ndc;
   //  pageRenderer.mmToNDC(page, mouse_mm, s_ndc);

   //  lines.addLine(Vec2(s_ndc) + Vec2(0, 0.01), Vec2(s_ndc) + Vec2(0, -0.01), Color(0, 1, 0, 1));
   //  lines.addLine(Vec2(s_ndc) + Vec2(0.01, 0), Vec2(s_ndc) + Vec2(-0.01, 0), Color(0, 1, 0, 1));
   m_lines.draw(camera.Transform());
}

void Renderer::shutdown()
{
    m_lines.shutdown();
}

void Renderer::renderPage(const Camera &camera, const PageModel &page, const Color &col)
{
   Vec2 a0 = Vec2(0, 0);
   Vec2 a1 = Vec2(page.page_width_mm, 0.0f);
   Vec2 a2 = Vec2(page.page_width_mm, page.page_height_mm);
   Vec2 a3 = Vec2(0.0f, page.page_height_mm);

   m_lines.addLine(a0, a1, col);
   m_lines.addLine(a1, a2, col);
   m_lines.addLine(a2, a3, col);
   m_lines.addLine(a3, a0, col);
}
