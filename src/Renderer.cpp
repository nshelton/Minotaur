#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include "filters/Types.h"
#include "core/Theme.h"

namespace
{
// Derive a content version for an entity's rendered (filter-chain output) image.
// It changes on any payload edit, filter add/remove, enable/disable, or parameter
// change, so the GL texture is only re-uploaded when the pixels actually change.
// Computed from the filter chain's generation counters (positional encoding of
// baseGen, filter count, and final filter generation; injective for realistic
// values). MUST be read before FilterChain::output() so the recorded version is
// never newer than the uploaded pixels (avoids a stale frame under the rare race
// where the background worker publishes between the two reads).
uint64_t entityImageVersion(const Entity &entity)
{
   const FilterChain &fc = entity.filterChain;
   size_t n = fc.size();
   uint64_t v = fc.baseGen();
   v = v * 1000003ull + static_cast<uint64_t>(n);
   if (n > 0)
      v = v * 1000003ull + fc.layerCacheAt(n - 1).gen;
   return v;
}
}

namespace
{
// Reserved BitmapRenderer texture id for the page fill quad. Entity ids are
// non-negative, so a negative id can never collide with one.
constexpr int kPageFillTexId = -1;

const Color kDarkBackground{0.1f, 0.1f, 0.12f, 1.0f};
const Color kLightBackground{0.5f, 0.5f, 0.5f, 1.0f};
}

Renderer::Renderer()
{
   m_lines.init();
   m_images.init();
   m_floatImages.init();

   m_pageFill.width_px = 1;
   m_pageFill.height_px = 1;
   m_pageFill.pixel_size_mm = 1.0f;
   m_pageFill.pixels = {255};
}

void Renderer::setDarkMode(bool dark)
{
   m_darkMode = dark;
   const Color &bg = dark ? kDarkBackground : kLightBackground;
   glClearColor(bg.r, bg.g, bg.b, bg.a);
}

void Renderer::render(const Camera &camera, const PageModel &page, const InteractionState &uiState)
{
   beginFrame(camera, page, uiState);
   endFrame(camera);
}

void Renderer::beginFrame(const Camera &camera, const PageModel &page, const InteractionState &uiState)
{
   m_lines.clear();
   m_images.clear();
   m_floatImages.clear();
   // draw page extent and grid
   renderPage(camera, page);

   // draw all entities
   for (const auto &[id, entity] : page.entities)
   {
      if (!entity.visible) {
         continue;
      }
      auto transform = entity.localToPage;
      // Read the content version before output() so it can never be newer than
      // the pixels we upload below (see entityImageVersion).
      const uint64_t imgVersion = entityImageVersion(entity);
      const LayerPtr layer = entity.filterChain.output();

      if (isPathSetLayer(layer))
      {
         const PathSet *psPtr = asPathSetConstPtr(layer);
         if (!psPtr)
            continue;
         const PathSet &ps = *psPtr;
         for (const auto &path : ps.paths)
         {
            Color pathCol = entity.color;
            if (!m_darkMode)
            {
               // Light mode: invert the layer color so default white ink
               // renders black on the white page.
               pathCol = Color(1.0f - pathCol.r, 1.0f - pathCol.g, 1.0f - pathCol.b, pathCol.a);
            }
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
         if (const Bitmap *bmptr = asBitmapConstPtr(layer))
         {
            const Bitmap &bm = *bmptr;
            m_images.addBitmap(id, bm, transform, imgVersion);
         }
         else if (const ColorImage *ciptr = asColorImageConstPtr(layer))
         {
            const ColorImage &ci = *ciptr;
            m_images.addColorImage(id, ci, transform, imgVersion);
         }
         else if (const FloatImage *fiptr = asFloatImageConstPtr(layer))
         {
            const FloatImage &fi = *fiptr;
            m_floatImages.addFloatImage(id, fi, transform, imgVersion);
         }
         else
         {
            continue; // unknown layer or not initialized
         }
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
         const LayerPtr selLayer = entity.filterChain.output();
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

}

void Renderer::endFrame(const Camera &camera)
{
   // Draw images first, then overlays/lines on top
   m_images.draw(camera.Transform());
   m_floatImages.draw(camera.Transform());
   m_lines.draw(camera.Transform());
}

void Renderer::shutdown()
{
   m_lines.shutdown();
   m_images.shutdown();
   m_floatImages.shutdown();
}

void Renderer::renderPage(const Camera &camera, const PageModel &page)
{
   if (!m_darkMode)
   {
      // White page fill, drawn before any entity so it sits underneath.
      // The 1x1 white bitmap is stretched over the full page extent.
      Mat3 pageScale = Mat3::scale(page.page_width_mm, page.page_height_mm);
      m_images.addBitmap(kPageFillTexId, m_pageFill, pageScale, 1);
   }

   Color outlineCol = m_darkMode ? Color(0.8f, 0.8f, 0.8f, 1.0f)
                                 : Color(0.35f, 0.35f, 0.35f, 1.0f);
   // outline

   drawRect(Vec2(0.0f, 0.0f), Vec2(page.page_width_mm, page.page_height_mm), outlineCol);

   // also show letter paper
   drawRect(Vec2(0.0f, 0.0f), Vec2(215.9f, 279.4f), outlineCol);
   drawRect(Vec2(0.0f, 0.0f), Vec2(279.4f, 215.9f), outlineCol);

   // grid lines every 10mm
   Color gridCol = m_darkMode ? Color(0.3f, 0.3f, 0.3f, 1.0f)
                              : Color(0.85f, 0.85f, 0.85f, 1.0f);
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
