#include "MainScreen.h"
#include "mvc/PathSetGenerator.h"
#include <string>

void MainScreen::onGui() {
    if (ImGui::Begin("Controls")) {
        ImGui::Text("Viewport");
        float zoom = m_viewport.scale();
        ImGui::Text("Zoom: %.2f", zoom);
        if (ImGui::Button("Reset View")) m_viewport.reset();
        ImGui::Separator();

        ImGui::Text("A3 Page");
        static int orient = 0; // 0 portrait, 1 landscape
        if (m_page.width_mm > m_page.height_mm) orient = 1; else orient = 0;
        int prev = orient;
        ImGui::RadioButton("Portrait", &orient, 0); ImGui::SameLine();
        ImGui::RadioButton("Landscape", &orient, 1);
        if (orient != prev) {
            m_page = (orient == 0) ? A3Page::Portrait() : A3Page::Landscape();
        }

        static float lineW = 1.5f;
        if (ImGui::SliderFloat("Outline Width", &lineW, 0.5f, 5.0f, "%.1f")) {
            m_lines.setLineWidth(lineW);
        }

        ImGui::Separator();
        ImGui::Text("Add Entities");
        float cx = m_page.width_mm * 0.5f;
        float cy = m_page.height_mm * 0.5f;
        if (ImGui::Button("Add Circle")) {
            auto ps = PathSetGenerator::Circle(cx, cy, 50.0f, 96, 0.9f, 0.2f, 0.2f, 1.0f);
            m_plot.addPathSet(std::move(ps));
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Square")) {
            auto ps = PathSetGenerator::Square(cx, cy, 80.0f, 0.2f, 0.7f, 0.9f, 1.0f);
            m_plot.addPathSet(std::move(ps));
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Star")) {
            auto ps = PathSetGenerator::Star(cx, cy, 60.0f, 30.0f, 5, 0.95f, 0.8f, 0.2f, 1.0f);
            m_plot.addPathSet(std::move(ps));
        }
    }
    ImGui::End();

    if (ImGui::Begin("Entities")) {
        ImGui::TextWrapped("Click on entities in the viewport to select and drag them around.");
        ImGui::Separator();
        for (size_t i = 0; i < m_plot.model().entities.size(); ++i) {
            ImGui::PushID(i);
            bool selected = (m_plot.activeIndex() == i);
            if (ImGui::Selectable(("Entity " + std::to_string(i)).c_str(), selected)) {
                m_plot.setActiveIndex(i);
            }
            ImGui::PopID();
        }
    }
    ImGui::End();

}
