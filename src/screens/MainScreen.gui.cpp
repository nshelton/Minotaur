#include "MainScreen.h"
#include "utils/PathSetGenerator.h"
#include "utils/BitmapGenerator.h"
#include <string>
#include <fmt/format.h>
#include "filters/FilterChain.h"
#include "filters/FilterRegistry.h"
#include "filters/Types.h"
#include "core/Theme.h"

void MainScreen::onGui()
{
    if (ImGui::Begin("Controls"))
    {
        ImGui::Text("Viewport");
        float zoom = m_camera.Transform().scale().x;
        ImGui::Text("scale: %.2f", zoom);

        Vec2 camPos = m_camera.Transform().translation();
        ImGui::Text("camPosX: %.2f", camPos.x);
        ImGui::Text("camPosY: %.2f", camPos.y);
        ImGui::Text("mousePixelX: %.2f", m_page.mouse_pixel.x);
        ImGui::Text("mousePixelY: %.2f", m_page.mouse_pixel.y);
        ImGui::Text("mouseMmX: %.2f", m_page.mouse_page_mm.x);
        ImGui::Text("mouse_mmY: %.2f", m_page.mouse_page_mm.y);
        if (ImGui::Button("Reset View"))
            m_camera.reset();
        ImGui::Separator();

        ImGui::Text("A3 Page");

        static float lineW = 1.5f;
        if (ImGui::SliderFloat("Outline Width", &lineW, 0.5f, 5.0f, "%.1f"))
        {
            m_renderer.setLineWidth(lineW);
        }

        bool showNodes = m_interaction.ShowPathNodes();
        if (ImGui::Checkbox("Show Path Nodes", &showNodes))
        {
            m_interaction.SetShowPathNodes(showNodes);
        }

        ImGui::Separator();
        ImGui::Text("Add Entities");
        Vec2 center = Vec2(m_page.page_width_mm, m_page.page_height_mm) * 0.5f;
        if (ImGui::Button("Add Circle"))
        {
            auto ps = PathSetGenerator::Circle(center, 50.0f, 96, Color(0.9f, 0.2f, 0.2f, 1.0f));
            m_page.addPathSet(std::move(ps));
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Square"))
        {
            auto ps = PathSetGenerator::Square(center, 80.0f, Color(0.2f, 0.7f, 0.9f, 1.0f));
            m_page.addPathSet(std::move(ps));
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Star"))
        {
            auto ps = PathSetGenerator::Star(center, 60.0f, 30.0f, 5, Color(0.95f, 0.8f, 0.2f, 1.0f));
            m_page.addPathSet(std::move(ps));
        }

        if (ImGui::Button("Add Gradient Bitmap"))
        {
            auto bm = BitmapGenerator::Gradient(256, 256, 0.5f);
            m_page.addBitmap(bm);
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Checker Bitmap"))
        {
            auto bm = BitmapGenerator::Checkerboard(256, 256, 16, 0.5f);
            m_page.addBitmap(bm);
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Radial Bitmap"))
        {
            auto bm = BitmapGenerator::Radial(256, 256, 0.5f);
            m_page.addBitmap(bm);
        }
    }
    ImGui::End();

    if (ImGui::Begin("Filter Chain"))
    {
        int selectedId = m_interaction.SelectedEntity().value_or(-1);
        if (selectedId >= 0 && m_page.entities.find(selectedId) != m_page.entities.end())
        {
            Entity &e = m_page.entities.at(selectedId);
            ImGui::Text("Selected Entity: %d", selectedId);
            ImGui::Text("Payload Version: %llu", static_cast<unsigned long long>(e.payloadVersion));
            ImGui::Text("Base Kind: %s", e.filterChain.baseKind() == LayerKind::Bitmap ? "Bitmap" : "PathSet");
            ImGui::Text("Base Gen: %llu", static_cast<unsigned long long>(e.filterChain.baseGen()));
            ImGui::Separator();

            size_t n = e.filterChain.filterCount();
            ImGui::Text("Filters: %llu", static_cast<unsigned long long>(n));

            // Add filter buttons based on the next input kind
            LayerKind nextIn = (n == 0)
                                   ? e.filterChain.baseKind()
                                   : e.filterChain.filterAt(n - 1)->outputKind();

            ImGui::Separator();
            ImGui::Text("Add Filter (next input: %s)", nextIn == LayerKind::Bitmap ? "Bitmap" : "PathSet");

            {
                const auto options = FilterRegistry::instance().byInput(nextIn);
                for (const auto &info : options)
                {
                    const std::string label = FilterRegistry::makeButtonLabel(info);
                    if (ImGui::Button(label.c_str()))
                    {
                        auto f = info.factory();
                        e.filterChain.addFilter(std::move(f));
                    }
                }
            }
            ImGui::Separator();

            std::optional<size_t> deleteIndex;
            for (size_t i = 0; i < n; ++i)
            {
                FilterBase *f = e.filterChain.filterAt(i);
                const LayerCache *lc = e.filterChain.layerCacheAt(i);
                if (!f || !lc)
                    continue;

                std::string nameString = fmt::format("{}###filterheader:{}", f->name(), i);
                if (ImGui::CollapsingHeader(nameString.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    // Inline delete button
                    if (ImGui::Button(fmt::format("Delete###deletefilter:{}", i).c_str()))
                    {
                        deleteIndex = i;
                    }

                    // Enable/disable toggle
                    bool enabled = e.filterChain.isFilterEnabled(i);
                    std::string enLabel = fmt::format("Enabled###enablefilter:{}", i);
                    if (ImGui::Checkbox(enLabel.c_str(), &enabled))
                    {
                        e.filterChain.setFilterEnabled(i, enabled);
                    }

                    ImGui::Text("Index: %llu", static_cast<unsigned long long>(i));
                    ImGui::Text("IO: %s -> %s",
                                f->inputKind() == LayerKind::Bitmap ? "Bitmap" : "PathSet",
                                f->outputKind() == LayerKind::Bitmap ? "Bitmap" : "PathSet");
                    ImGui::Text("Param Ver: %llu", static_cast<unsigned long long>(f->paramVersion()));
                    ImGui::Text("Cache: %s", lc->valid ? "valid" : "invalid");
                    ImGui::Text("Cache Gen: %llu", static_cast<unsigned long long>(lc->gen));
                    ImGui::Text("Upstream Gen: %llu", static_cast<unsigned long long>(lc->upstreamGen));
                    ImGui::Text("Last time: %.3f ms", f->lastRunMs());

                    // Parameter controls
                    for (auto &[paramKey, param] : f->m_parameters)
                    {
                        if (paramKey == "threshold")
                        {
                            std::string label = fmt::format("{}###param:{}:{}", param.name, paramKey, i);

                            float val = param.value;
                            if (ImGui::SliderFloat(label.c_str(), &val, param.minValue, param.maxValue))
                            {
                                f->setParameter(paramKey, val);
                            }
                        }
                    }
                }
            }

            if (deleteIndex.has_value())
            {
                e.filterChain.removeFilter(*deleteIndex);
            }
        }
        else
        {
            ImGui::Text("No entity selected.");
        }
    }
    ImGui::End();

    if (ImGui::Begin("Entities"))
    {
        ImGui::Text("total vertices: %d", m_renderer.totalVertices());
        ImGui::Text("Hovered Entity: %s",
                    m_interaction.HoveredEntity() ? std::to_string(*m_interaction.HoveredEntity()).c_str() : "None");
        ImGui::Text("Selected Entity: %s",
                    m_interaction.SelectedEntity() ? std::to_string(*m_interaction.SelectedEntity()).c_str() : "None");
        ImGui::Separator();

        // To avoid iterator invalidation, collect IDs to delete in a separate vector
        std::vector<int> toDelete;
        if (ImGui::BeginTable("EntitiesTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Entity");
            ImGui::TableSetupColumn("Select", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Delete", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            for (const auto &[id, entity] : m_page.entities)
            {
                ImGui::TableNextRow();

                bool selected = (m_interaction.state().activeId.has_value() && m_interaction.state().activeId.value() == id);
                std::string label = fmt::format("Entity {}, ({:.1f},{:.1f})", id, entity.localToPage.translation().x, entity.localToPage.translation().y);

                // Column 0: Label (non-interactive text to avoid accidental clicks)
                ImGui::PushID(id * 2 + 0);
                ImGui::TableSetColumnIndex(0);
                // Color label by current output kind (vector vs raster)
                const LayerPtr &layer = const_cast<Entity &>(m_page.entities.at(id)).filterChain.output();
                const Color &c = isPathSetLayer(layer) ? theme::PathsetColor : theme::BitmapColor;
                ImVec4 txtCol(c.r, c.g, c.b, c.a);
                ImGui::TextColored(txtCol, "%s", label.c_str());

                // Column 1: Select button
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button("Select"))
                {
                    m_interaction.SelectEntity(id);
                }
                ImGui::PopID();

                // Column 2: Delete button
                ImGui::PushID(id * 2 + 1);
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button("x"))
                {
                    toDelete.push_back(id);
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        // Actually delete entities after the loop
        for (int id : toDelete)
        {
            m_page.entities.erase(id);
            // Also deselect if the active/hovered entity was deleted
            if (m_interaction.state().activeId && m_interaction.state().activeId.value() == id)
            {
                m_interaction.DeselectEntity();
            }
            if (m_interaction.state().hoveredId && m_interaction.state().hoveredId.value() == id)
            {
                m_interaction.ClearHover();
            }
        }
    }
    ImGui::End();
}
