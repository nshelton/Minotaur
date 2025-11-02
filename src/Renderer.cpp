#include "Renderer.h"

#include <algorithm>

Renderer::Renderer()
{
   m_lines.init();
}

void Renderer::render(const Camera &camera, const PageModel &page, const InteractionState &uiState)
{
   m_lines.clear();
   // draw page extent and grid
   renderPage(camera, page);

   // draw all pathsets
   for (const auto &[id, entity] : page.entities)
   {
      auto transform = entity.localToPage;

      for (const auto &path : entity.pathset.paths)
      {
         Color pathCol = Color(0.9f, 1.0f, 0.9f, 1.0f);
         for (size_t i = 1; i < path.points.size(); ++i)
         {
            m_lines.addLine(transform * path.points[i - 1], transform * path.points[i], pathCol);
         }
         if (path.closed && path.points.size() > 2)
         {
            m_lines.addLine(transform * path.points.back(), transform * path.points.front(), pathCol);
         }
      }
   }

   // draw hovered
   if (uiState.hoveredId)
   {
      if (page.entities.find(*uiState.hoveredId) != page.entities.end())
      {
         const Entity &entity = page.entities.at(*uiState.hoveredId);
         drawRect(
            entity.localToPage * entity.pathset.aabb.min,
            entity.localToPage * entity.pathset.aabb.max,
            Color(0, 0.5, 0, 0.5));
      }
   }

   // draw selected
   if (uiState.activeId)
   {
      if (page.entities.find(*uiState.activeId) != page.entities.end())
      {
         const Entity &entity = page.entities.at(*uiState.activeId);
         drawRect(
            entity.localToPage * entity.pathset.aabb.min,
            entity.localToPage * entity.pathset.aabb.max,
            Color(0.8f, 0.8f, 0.0f, 0.8f));
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

   drawRect(Vec2(0.0f, 0.0f), Vec2(page.page_width_mm, page.page_height_mm), outlineCol);

   // grid lines every 10mm
   Color gridCol = Color(0.3f, 0.3f, 0.3f, 1.0f);
   for (float x = 10.0f; x < page.page_width_mm; x += 10.0f)
   {
      m_lines.addLine(Vec2(x, 0.0f), Vec2(x, page.page_height_mm), gridCol);
   }
   for (float y = 10.0f; y < page.page_height_mm; y += 10.0f)
   {
      m_lines.addLine(Vec2(0.0f, y), Vec2(page.page_width_mm, y), gridCol);
   }
}

void Renderer::drawRect(const Vec2 &min, const Vec2 &max, const Color &col)
{
   m_lines.addLine(Vec2(min.x, min.y), Vec2(max.x, min.y), col);
   m_lines.addLine(Vec2(max.x, min.y), Vec2(max.x, max.y), col);
   m_lines.addLine(Vec2(max.x, max.y), Vec2(min.x, max.y), col);
   m_lines.addLine(Vec2(min.x, max.y), Vec2(min.x, min.y), col);
}
