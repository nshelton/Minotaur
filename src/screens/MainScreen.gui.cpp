#include "MainScreen.h"
#include "utils/PathSetGenerator.h"
#include "utils/BitmapGenerator.h"
#include <string>
#include <fmt/format.h>

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
                if (selected)
                {
                    ImGui::TextColored(ImVec4(1, 0.9f, 0.2f, 1), "%s", label.c_str());
                }
                else
                {
                    ImGui::Text("%s", label.c_str());
                }

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
        for (int id : toDelete) {
            m_page.entities.erase(id);
            // Also deselect if the active/hovered entity was deleted
            if (m_interaction.state().activeId && m_interaction.state().activeId.value() == id) {
                m_interaction.DeselectEntity();
            }
            if (m_interaction.state().hoveredId && m_interaction.state().hoveredId.value() == id) {
                m_interaction.ClearHover();
            }
        }

    }
    ImGui::End();
}
