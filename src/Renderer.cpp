#include "Renderer.h"

#include <algorithm>


Renderer::Renderer()
{
    m_lines.init();
}

void Renderer::render(const Camera &camera, const PageModel &page, const InteractionState &uiState)
{
   // draw page extent and grid
   renderPage(camera, page);

   for (const auto &ps : page.entities)
   {
      for (const auto &path : ps.paths)
      {
         // add lines from path here
      }
   }

   m_lines.draw(camera.Transform());
}

void Renderer::shutdown()
{
    m_lines.shutdown();
}

void Renderer::renderPage(const Camera &camera, const PageModel &page)
{
   Color outlineCol = Color(0.8f, 0.8f, 0.8f, 1.0f);
   // outline
   Vec2 a0 = Vec2(0, 0);
   Vec2 a1 = Vec2(page.page_width_mm, 0.0f);
   Vec2 a2 = Vec2(page.page_width_mm, page.page_height_mm);
   Vec2 a3 = Vec2(0.0f, page.page_height_mm);

   m_lines.addLine(a0, a1, outlineCol);
   m_lines.addLine(a1, a2, outlineCol);
   m_lines.addLine(a2, a3, outlineCol);
   m_lines.addLine(a3, a0, outlineCol);

   // grid lines every 10mm
   Color gridCol = Color(0.6f, 0.6f, 0.6f, 1.0f);
   for (float x = 10.0f; x < page.page_width_mm; x += 10.0f)
   {
      m_lines.addLine(Vec2(x, 0.0f), Vec2(x, page.page_height_mm), gridCol);
   }
   for (float y = 10.0f; y < page.page_height_mm; y += 10.0f)
   {
      m_lines.addLine(Vec2(0.0f, y), Vec2(page.page_width_mm, y), gridCol);
   }
   
}
