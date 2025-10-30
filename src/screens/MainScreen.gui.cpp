#include "MainScreen.h"

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
    }
    ImGui::End();
}

