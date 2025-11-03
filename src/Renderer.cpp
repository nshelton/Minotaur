#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include "filters/Types.h"
#include "core/Theme.h"

Renderer::Renderer()
{
   m_lines.init();
   m_images.init();
}

void Renderer::render(const Camera &camera, const PageModel &page, const InteractionState &uiState)
{
   m_lines.clear();
   m_images.clear();
   // draw page extent and grid
   renderPage(camera, page);

   // draw all entities
   for (const auto &[id, entity] : page.entities)
   {
      auto transform = entity.localToPage;
      const LayerPtr &layer = const_cast<Entity &>(entity).filterChain.output();

      if (isPathSetLayer(layer))
      {
         const PathSet *psPtr = asPathSetConstPtr(layer);
         if (!psPtr)
            continue;
         const PathSet &ps = *psPtr;
         for (const auto &path : ps.paths)
         {
            Color pathCol = theme::PathsetColor;
            for (size_t i = 1; i < path.points.size(); ++i)
            {
               m_lines.addLine(transform * path.points[i - 1], transform * path.points[i], pathCol);
            }
            if (path.closed && path.points.size() > 2)
            {
               m_lines.addLine(transform * path.points.back(), transform * path.points.front(), pathCol);
            }

            // Optionally render path vertices as filled points
            if (uiState.showPathNodes)
            {
               m_lines.setPointDiameterPx(m_nodeDiameterPx);
               for (const Vec2 &pLocal : path.points)
               {
                  Vec2 p = transform.apply(pLocal);
                  m_lines.addPoint(p, pathCol);
               }
            }
         }
      }
      else
      {
         const Bitmap *bmptr = asBitmapConstPtr(layer);
         if (!bmptr)
            continue; // layer not initialized yet
         const Bitmap &bm = *bmptr;
         // Queue bitmap for textured draw
         m_images.addBitmap(id, bm, transform);
      }
   }

   // draw hovered
   if (uiState.hoveredId)
   {
      if (page.entities.find(*uiState.hoveredId) != page.entities.end())
      {
         const Entity &entity = page.entities.at(*uiState.hoveredId);
         BoundingBox bb = entity.boundsLocal();
         drawRect(
             entity.localToPage * bb.min - 2,
             entity.localToPage * bb.max + 2,
             Color(0, 1, 0, 1));

         // draw resize handles on hover as tooltips
         Vec2 minL = bb.min;
         Vec2 maxL = bb.max;
         Vec2 mid = Vec2((minL.x + maxL.x) * 0.5f, (minL.y + maxL.y) * 0.5f);
         Vec2 handles[8] = {
             Vec2(mid.x, maxL.y),  // N
             Vec2(mid.x, minL.y),  // S
             Vec2(maxL.x, mid.y),  // E
             Vec2(minL.x, mid.y),  // W
             Vec2(maxL.x, maxL.y), // NE
             Vec2(minL.x, maxL.y), // NW
             Vec2(maxL.x, minL.y), // SE
             Vec2(minL.x, minL.y)  // SW
         };

         // handle render size
         Color hc = Color(0.2f, 0.9f, 0.2f, 1.0f);
         for (Vec2 pLocal : handles)
         {
            Vec2 p = entity.localToPage.apply(pLocal);
            drawHandle(p, HANDLE_RENDER_RADIUS_MM, hc);
         }
      }
   }

   // draw selected
   if (uiState.activeId)
   {
      if (page.entities.find(*uiState.activeId) != page.entities.end())
      {
         const Entity &entity = page.entities.at(*uiState.activeId);
         BoundingBox bb = entity.boundsLocal();
         // Outline color reflects current output kind (vector vs raster)
         const LayerPtr &selLayer = const_cast<Entity &>(entity).filterChain.output();
         Color selCol = isPathSetLayer(selLayer) ? theme::PathsetColor : theme::BitmapColor;
         drawRect(
             entity.localToPage * bb.min - 1,
             entity.localToPage * bb.max + 1,
             selCol);

         // draw resize handles (corners + edges)
         Vec2 minL = bb.min;
         Vec2 maxL = bb.max;
         Vec2 mid = Vec2((minL.x + maxL.x) * 0.5f, (minL.y + maxL.y) * 0.5f);
         Vec2 handles[8] = {
             Vec2(mid.x, maxL.y),  // N
             Vec2(mid.x, minL.y),  // S
             Vec2(maxL.x, mid.y),  // E
             Vec2(minL.x, mid.y),  // W
             Vec2(maxL.x, maxL.y), // NE
             Vec2(minL.x, maxL.y), // NW
             Vec2(maxL.x, minL.y), // SE
             Vec2(minL.x, minL.y)  // SW
         };

         Color hc = Color(1.0f, 0.6f, 0.1f, 1.0f);
         for (Vec2 pLocal : handles)
         {
            Vec2 p = entity.localToPage.apply(pLocal);
            drawHandle(p, HANDLE_RENDER_RADIUS_MM, hc);
         }
      }
   }

   // Draw images first, then overlays/lines on top
   m_images.draw(camera.Transform());
   m_lines.draw(camera.Transform());
}

void Renderer::shutdown()
{
   m_lines.shutdown();
   m_images.shutdown();
}

void Renderer::renderPage(const Camera &camera, const PageModel &page)
{
   Color outlineCol = Color(0.8f, 0.8f, 0.8f, 1.0f);
   // outline

   drawRect(Vec2(0.0f, 0.0f), Vec2(page.page_width_mm, page.page_height_mm), outlineCol);

   // also show letter paper
   drawRect(Vec2(0.0f, 0.0f), Vec2(215.9f, 279.4f), outlineCol);
   drawRect(Vec2(0.0f, 0.0f), Vec2(279.4f, 215.9f), outlineCol);

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

void Renderer::drawHandle(const Vec2 &center, float sizeMm, const Color &col)
{
   Vec2 half(sizeMm * 0.5f, sizeMm * 0.5f);
   drawRect(center - half, center + half, col);
}

void Renderer::drawCircle(const Vec2 &center, float radiusMm, const Color &col)
{
   const int segments = 16;
   if (segments < 3)
      return;
   float twoPi = 6.28318530718f;
   Vec2 prev = Vec2(center.x + radiusMm, center.y);
   for (int i = 1; i <= segments; ++i)
   {
      float t = (float)i / (float)segments;
      float ang = t * twoPi;
      Vec2 cur = Vec2(center.x + radiusMm * cosf(ang), center.y + radiusMm * sinf(ang));
      m_lines.addLine(prev, cur, col);
      prev = cur;
   }
}
