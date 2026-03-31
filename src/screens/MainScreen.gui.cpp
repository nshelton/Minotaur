#include "MainScreen.h"
#include <string>
#include <cmath>
#include <fmt/format.h>
#include "filters/FilterChain.h"
#include "filters/FilterRegistry.h"
#include "filters/Types.h"
#include "generators/GeneratorRegistry.h"
#include "generators/mesh/MeshGenerator.h"
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
        ImGui::Text("mousePixelX: %.2f", m_interaction.state().mouse_pixel.x);
        ImGui::Text("mousePixelY: %.2f", m_interaction.state().mouse_pixel.y);
        ImGui::Text("mouseMmX: %.2f", m_interaction.state().mouse_page_mm.x);
        ImGui::Text("mouse_mmY: %.2f", m_interaction.state().mouse_page_mm.y);
        if (ImGui::Button("Reset View"))
            m_camera.reset();
        ImGui::Separator();

        ImGui::Text("A3 Page");

        static float lineW = 1.5f;
        if (ImGui::SliderFloat("Outline Width", &lineW, 0.5f, 5.0f, "%.1f"))
        {
            m_renderer.setLineWidth(lineW);
        }

        static float nodePx = 8.0f;
        if (ImGui::SliderFloat("Node Size (px)", &nodePx, 2.0f, 24.0f, "%.0f"))
        {
            m_renderer.setNodeDiameterPx(nodePx);
        }

        bool showNodes = m_interaction.ShowPathNodes();
        if (ImGui::Checkbox("Show Path Nodes", &showNodes))
        {
            m_interaction.SetShowPathNodes(showNodes);
        }

        ImGui::Separator();
        {
            Vec2 center = Vec2(m_page.page_width_mm, m_page.page_height_mm) * 0.5f;
            const auto &gens = GeneratorRegistry::instance().all();
            auto toImVec4 = [](const Color &c) { return ImVec4(c.r, c.g, c.b, c.a); };
            auto lighten  = [](ImVec4 c, float s) {
                auto cl = [](float v){ return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
                c.x = cl(c.x * s); c.y = cl(c.y * s); c.z = cl(c.z * s);
                return c;
            };
            auto drawGenButtons = [&](LayerKind kind)
            {
                Color gc = (kind == LayerKind::PathSet) ? theme::PathsetColor : theme::BitmapColor;
                ImVec4 b  = toImVec4(gc);
                ImVec4 bh = lighten(b, 1.08f);
                ImVec4 ba = lighten(b, 1.16f);
                for (const auto &info : gens)
                {
                    if (info.outputKind != kind) continue;
                    ImGui::PushStyleColor(ImGuiCol_Button,        b);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bh);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ba);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
                    if (ImGui::Button(info.name.c_str()))
                        m_page.addGeneratedEntity(info.factory(), center);
                    ImGui::PopStyleColor(4);
                    ImGui::SameLine();
                }
                ImGui::NewLine();
            };
            if (ImGui::CollapsingHeader("Path Generators", ImGuiTreeNodeFlags_DefaultOpen))
                drawGenButtons(LayerKind::PathSet);
            if (ImGui::CollapsingHeader("Bitmap Generators", ImGuiTreeNodeFlags_DefaultOpen))
                drawGenButtons(LayerKind::Bitmap);
        }

        ImGui::Separator();
        m_plotter.renderGui(m_page);
    }
    ImGui::End();

    if (ImGui::Begin("Filter Chain"))
    {
        int selectedId = m_interaction.SelectedEntity().value_or(-1);
        if (selectedId >= 0 && m_page.entities.find(selectedId) != m_page.entities.end())
        {
            Entity &e = m_page.entities.at(selectedId);
            ImGui::Text("Selected Entity: %d", selectedId);
            auto kindToString = [](LayerKind k) {
                return (k == LayerKind::Bitmap) ? "Bitmap" : (k == LayerKind::PathSet ? "PathSet" : "Float");
            };

            // --- Generator panel (shown only for parametric entities) ---
            if (e.generator)
            {
                Color gc = (e.generator->outputKind() == LayerKind::PathSet)
                    ? theme::PathsetColor : theme::BitmapColor;
                auto toImVec4g = [](const Color &c) { return ImVec4(c.r, c.g, c.b, c.a); };
                auto lighten   = [](ImVec4 c, float s) {
                    auto cl = [](float v){ return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
                    c.x = cl(c.x * s); c.y = cl(c.y * s); c.z = cl(c.z * s);
                    return c;
                };
                ImVec4 hdr  = toImVec4g(gc);
                ImVec4 hdrH = lighten(hdr, 1.10f);
                ImVec4 hdrA = lighten(hdr, 1.20f);
                ImGui::PushStyleColor(ImGuiCol_Header,        hdr);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hdrH);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,  hdrA);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
                std::string genHeader = fmt::format("{}  [generator]###genheader", e.generator->name());
                bool genOpen = ImGui::CollapsingHeader(genHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::PopStyleColor(4);

                if (genOpen)
                {
                    // String parameters (text inputs, file paths, etc.)
                    for (auto &[paramKey, value] : e.generator->m_stringParameters)
                    {
                        // Per-entity string buffer stored in a map keyed by entity id + param key
                        std::string bufKey = fmt::format("{}:{}", selectedId, paramKey);
                        auto &buf = m_genStringBufs[bufKey];
                        // Sync buffer from model if it appears out of date
                        if (buf != value)
                            buf = value;

                        std::string label = fmt::format("{}###genstr:{}", paramKey, bufKey);
                        char tmp[1024];
                        strncpy(tmp, buf.c_str(), sizeof(tmp) - 1);
                        tmp[sizeof(tmp) - 1] = '\0';
                        if (ImGui::InputText(label.c_str(), tmp, sizeof(tmp)))
                        {
                            buf = tmp;
                            e.generator->setStringParameter(paramKey, buf);
                        }
                    }

                    // Float/Int/Bool/Enum parameters
                    for (auto &[paramKey, param] : e.generator->m_parameters)
                    {
                        std::string label = fmt::format("{}###genparam:{}", param.name, paramKey);
                        switch (param.type)
                        {
                        case FilterParameter::Int:
                        {
                            int ival = static_cast<int>(std::lround(param.value));
                            if (ImGui::SliderInt(label.c_str(), &ival,
                                                 static_cast<int>(param.minValue),
                                                 static_cast<int>(param.maxValue)))
                                e.generator->setParameter(paramKey, static_cast<float>(ival));
                            break;
                        }
                        case FilterParameter::Bool:
                        {
                            bool bval = param.value > 0.5f;
                            if (ImGui::Checkbox(label.c_str(), &bval))
                                e.generator->setParameter(paramKey, bval ? 1.0f : 0.0f);
                            break;
                        }
                        case FilterParameter::Enum:
                        {
                            int cur = static_cast<int>(std::lround(param.value));
                            if (!param.enumLabels.empty())
                            {
                                const char *preview = (cur >= 0 && cur < static_cast<int>(param.enumLabels.size()))
                                    ? param.enumLabels[cur].c_str() : "?";
                                if (ImGui::BeginCombo(label.c_str(), preview))
                                {
                                    for (int ei = 0; ei < static_cast<int>(param.enumLabels.size()); ++ei)
                                    {
                                        bool sel = (ei == cur);
                                        if (ImGui::Selectable(param.enumLabels[ei].c_str(), sel))
                                            e.generator->setParameter(paramKey, static_cast<float>(ei));
                                        if (sel) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }
                            }
                            break;
                        }
                        case FilterParameter::Precise:
                        {
                            float val = param.value;
                            if (ImGui::InputFloat(label.c_str(), &val, 0.0f, 0.0f, "%.6f"))
                            {
                                val = std::max(param.minValue, std::min(val, param.maxValue));
                                e.generator->setParameter(paramKey, val);
                            }
                            break;
                        }
                        case FilterParameter::Float:
                        default:
                        {
                            float val = param.value;
                            if (ImGui::SliderFloat(label.c_str(), &val, param.minValue, param.maxValue))
                                e.generator->setParameter(paramKey, val);
                            break;
                        }
                        }
                    }
                    // --- Mesh generator: inline 3D preview with trackball ---
                    if (auto *meshGen = dynamic_cast<MeshGenerator *>(e.generator.get()))
                    {
                        if (meshGen->hasMesh())
                        {
                            ImGui::Separator();
                            ImGui::Text("3D Preview (drag to rotate)");

                            // Dynamic size: fill available panel width, square aspect
                            float avail = ImGui::GetContentRegionAvail().x;
                            float previewSize = std::max(128.0f, avail);
                            int pw = static_cast<int>(previewSize);
                            int ph = static_cast<int>(previewSize);

                            // Sync rotation from generator params to preview widget
                            float rx = meshGen->m_parameters.at("rotX").value;
                            float ry = meshGen->m_parameters.at("rotY").value;
                            float rz = meshGen->m_parameters.at("rotZ").value;
                            meshGen->previewWidget().setRotation(rx, ry, rz);
                            meshGen->previewWidget().render(pw, ph);

                            GLuint tex = meshGen->previewWidget().texture();
                            if (tex)
                            {
                                // Use InvisibleButton for mouse capture, draw image via draw list
                                ImVec2 pos = ImGui::GetCursorScreenPos();
                                ImGui::InvisibleButton("##meshPreviewDrag", ImVec2(previewSize, previewSize));
                                bool isDragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);

                                // Draw the FBO texture over the button (flip V because FBO is bottom-up)
                                ImGui::GetWindowDrawList()->AddImage(
                                    static_cast<ImTextureID>(static_cast<uintptr_t>(tex)),
                                    pos, ImVec2(pos.x + previewSize, pos.y + previewSize),
                                    ImVec2(0, 1), ImVec2(1, 0));

                                // Trackball: drag on the image to rotate
                                if (isDragging)
                                {
                                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                                    // Horizontal drag -> rotY, vertical drag -> rotX
                                    float sensitivity = 0.5f;
                                    float newRy = ry + delta.x * sensitivity;
                                    float newRx = rx + delta.y * sensitivity;
                                    // Clamp to parameter range
                                    newRy = std::max(-180.0f, std::min(180.0f, newRy));
                                    newRx = std::max(-180.0f, std::min(180.0f, newRx));
                                    meshGen->setParameter("rotY", newRy);
                                    meshGen->setParameter("rotX", newRx);
                                }
                            }
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "No mesh loaded. Drop an .obj file or set the file path.");
                        }

                        // Load OBJ button
                        if (ImGui::Button("Reload OBJ"))
                        {
                            meshGen->onStringParameterChanged("file");
                        }
                    }
                }
                ImGui::Separator();
            }

            ImGui::Text("Base Kind: %s", kindToString(e.filterChain.baseKind()));
            // ImGui::Text("Base Gen: %llu", static_cast<unsigned long long>(e.filterChain.baseGen()));
            // ImGui::Separator();

            size_t n = e.filterChain.filterCount();
            ImGui::Text("Filters: %llu", static_cast<unsigned long long>(n));

            // Add filter buttons based on the next input kind
            LayerKind nextIn = (n == 0)
                                   ? e.filterChain.baseKind()
                                   : e.filterChain.filterAt(n - 1)->outputKind();

            ImGui::Separator();
            ImGui::Text("Add Filter (next input: %s)", kindToString(nextIn));

            {
                const auto options = FilterRegistry::instance().byInput(nextIn);
                auto toImVec4 = [](const Color &c) { return ImVec4(c.r, c.g, c.b, c.a); };
                auto lighten = [](ImVec4 c, float s) {
                    auto clamp01 = [](float v){ return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
                    c.x = clamp01(c.x * s);
                    c.y = clamp01(c.y * s);
                    c.z = clamp01(c.z * s);
                    return c;
                };
                for (const auto &info : options)
                {
                    // Button color based on filter IO kinds; blend if converting
                    auto kindColor = [](LayerKind k) {
                        // Treat Float as raster color for UI
                        return (k == LayerKind::PathSet) ? theme::PathsetColor : theme::BitmapColor;
                    };
                    Color cin = kindColor(info.inputKind);
                    Color cout = kindColor(info.outputKind);
                    Color cbase = (info.inputKind != info.outputKind)
                        ? Color((cin.r + cout.r) * 0.5f, (cin.g + cout.g) * 0.5f, (cin.b + cout.b) * 0.5f, 1.0f)
                        : cout;

                    ImVec4 b  = toImVec4(cbase);
                    ImVec4 bh = lighten(b, 1.08f);
                    ImVec4 ba = lighten(b, 1.16f);
                    ImGui::PushStyleColor(ImGuiCol_Button, b);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bh);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ba);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));

                    const std::string label = info.name;

                    if (ImGui::Button(label.c_str()))
                    {
                        auto f = info.factory();
                        e.filterChain.addFilter(std::move(f));
                    }

                    ImGui::PopStyleColor(4);
                }
            }
            ImGui::Separator();

            bool deleteFailed = false;
            std::optional<size_t> deleteIndex;
            for (size_t i = 0; i < n; ++i)
            {
                FilterBase *f = e.filterChain.filterAt(i);
                const LayerCache *lc = e.filterChain.layerCacheAt(i);
                if (!f || !lc)
                    continue;

                // Per-filter header color based on IO kinds
                auto toImVec4 = [](const Color &c) { return ImVec4(c.r, c.g, c.b, c.a); };
                auto lighten = [](ImVec4 c, float s) {
                    auto clamp01 = [](float v){ return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
                    c.x = clamp01(c.x * s);
                    c.y = clamp01(c.y * s);
                    c.z = clamp01(c.z * s);
                    return c;
                };

                const bool inBitmap = (f->inputKind() == LayerKind::Bitmap);
                const bool outBitmap = (f->outputKind() == LayerKind::Bitmap);
                Color cin = inBitmap ? theme::BitmapColor : theme::PathsetColor;
                Color cout = outBitmap ? theme::BitmapColor : theme::PathsetColor;

                // If it converts types, blend half-way; else use the IO color
                Color cbase;
                if (inBitmap != outBitmap)
                {
                    cbase = Color(
                        (cin.r + cout.r) * 0.5f,
                        (cin.g + cout.g) * 0.5f,
                        (cin.b + cout.b) * 0.5f,
                        1.0f);
                }
                else
                {
                    cbase = cout; // same either way
                }

                ImVec4 hdr = toImVec4(cbase);
                ImVec4 hdrHovered = lighten(hdr, 1.10f);
                ImVec4 hdrActive  = lighten(hdr, 1.20f);

                ImGui::PushStyleColor(ImGuiCol_Header, hdr);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hdrHovered);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, hdrActive);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));

                std::string nameString = fmt::format("{}###filterheader:{}", f->name(), i);
                bool open = ImGui::CollapsingHeader(nameString.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::PopStyleColor(4);

                if (open)
                {

                    // Enable/disable toggle with type-safety gating
                    bool enabled = e.filterChain.isFilterEnabled(i);
                    bool canEnable = e.filterChain.canEnableFilterAtIndex(i);
                    bool canDisable = e.filterChain.canDisableFilterAtIndex(i);
                    bool allowToggle = enabled ? canDisable : canEnable;
                    std::string enLabel = fmt::format("Enabled###enablefilter:{}", i);
                    if (!allowToggle) ImGui::BeginDisabled();
                    if (ImGui::Checkbox(enLabel.c_str(), &enabled))
                    {
                        e.filterChain.setFilterEnabled(i, enabled);
                    }
                    if (!allowToggle) ImGui::EndDisabled();
                    ImGui::SameLine();

                    // Inline delete button (disabled if removing would break typing)
                    bool canRemove = e.filterChain.canRemoveFilterAtIndex(i);
                    if (!canRemove) ImGui::BeginDisabled();
                    if (ImGui::Button(fmt::format("Delete###deletefilter:{}", i).c_str()))
                    {
                        deleteIndex = i;
                    }
                    if (!canRemove) ImGui::EndDisabled();

                    // ImGui::Text("Index: %llu", static_cast<unsigned long long>(i));
                    // ImGui::Text("IO: %s -> %s",
                    // ? "Bitmap" : "PathSet",
                    // f->outputKind() == LayerKind::Bitmap ? "Bitmap" : "PathSet");
                    // ImGui::Text("Param Ver: %llu", static_cast<unsigned long long>(f->paramVersion()));
                    // ImGui::Text("Cache: %s", lc->valid ? "valid" : "invalid");
                    // ImGui::Text("Cache Gen: %llu", static_cast<unsigned long long>(lc->gen));
                    // ImGui::Text("Upstream Gen: %llu", static_cast<unsigned long long>(lc->upstreamGen));

                    // Parameter controls
                    for (auto &[paramKey, param] : f->m_parameters)
                    {
                        std::string label = fmt::format("{}###param:{}:{}", param.name, paramKey, i);

                        switch (param.type)
                        {
                        case FilterParameter::Int:
                        {
                            int ival = static_cast<int>(std::lround(param.value));
                            if (ImGui::SliderInt(label.c_str(), &ival,
                                                 static_cast<int>(param.minValue),
                                                 static_cast<int>(param.maxValue)))
                            {
                                f->setParameter(paramKey, static_cast<float>(ival));
                            }
                            break;
                        }
                        case FilterParameter::Bool:
                        {
                            bool bval = param.value > 0.5f;
                            if (ImGui::Checkbox(label.c_str(), &bval))
                            {
                                f->setParameter(paramKey, bval ? 1.0f : 0.0f);
                            }
                            break;
                        }
                        case FilterParameter::Enum:
                        {
                            int cur = static_cast<int>(std::lround(param.value));
                            if (!param.enumLabels.empty())
                            {
                                if (ImGui::BeginCombo(label.c_str(),
                                    (cur >= 0 && cur < static_cast<int>(param.enumLabels.size()))
                                        ? param.enumLabels[cur].c_str() : "?"))
                                {
                                    for (int ei = 0; ei < static_cast<int>(param.enumLabels.size()); ++ei)
                                    {
                                        bool selected = (ei == cur);
                                        if (ImGui::Selectable(param.enumLabels[ei].c_str(), selected))
                                        {
                                            f->setParameter(paramKey, static_cast<float>(ei));
                                        }
                                        if (selected) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }
                            }
                            break;
                        }
                        case FilterParameter::Precise:
                        {
                            float val = param.value;
                            if (ImGui::InputFloat(label.c_str(), &val, 0.0f, 0.0f, "%.6f"))
                            {
                                val = std::max(param.minValue, std::min(val, param.maxValue));
                                f->setParameter(paramKey, val);
                            }
                            break;
                        }
                        case FilterParameter::Float:
                        default:
                        {
                            float val = param.value;
                            if (ImGui::SliderFloat(label.c_str(), &val, param.minValue, param.maxValue))
                            {
                                f->setParameter(paramKey, val);
                            }
                            break;
                        }
                        }
                    }

                    // Show progress bar while filter is computing
                    if (f->isRunning())
                    {
                        float prog = f->progress();
                        ImGui::ProgressBar(prog, ImVec2(-1, 0));
                        ImGui::Text("Computing... %.0f%%", prog * 100.0f);
                    }
                    else if (f->outputKind() == LayerKind::PathSet)
                    {
                        std::string ioinfo = fmt::format(
                            "Output {:.3f} ms | {} verts | {} paths",
                            f->lastRunMs(),
                            f->lastVertexCount(),
                            f->lastPathCount());
                        ImGui::TextUnformatted(ioinfo.c_str());
                    }

                    else if (f->outputKind() == LayerKind::Bitmap)
                    {
                        const LayerCache *lcPtr = e.filterChain.layerCacheAt(i);
                        size_t w = 0, h = 0;
                        if (lcPtr && lcPtr->data)
                        {
                            if (const Bitmap *bp = asBitmapConstPtr(lcPtr->data)) { w = bp->width_px; h = bp->height_px; }
                        }
                        std::string ioinfo = fmt::format(
                            "Output {:.3f} ms, {}x{}",
                            f->lastRunMs(),
                            w, h);
                        ImGui::TextUnformatted(ioinfo.c_str());
                    }

                    else if (f->outputKind() == LayerKind::FloatImage)
                    {
                        const LayerCache *lcPtr = e.filterChain.layerCacheAt(i);
                        size_t w = 0, h = 0;
                        float vmin = 0.0f, vmax = 0.0f;
                        if (lcPtr && lcPtr->data)
                        {
                            if (const FloatImage *fp = asFloatImageConstPtr(lcPtr->data)) { w = fp->width_px; h = fp->height_px; vmin = fp->minValue; vmax = fp->maxValue; }
                        }
                        std::string ioinfo = fmt::format(
                            "Output {:.3f} ms, {}x{} (float)  min {:.3f}  max {:.3f}",
                            f->lastRunMs(),
                            w, h, vmin, vmax);
                        ImGui::TextUnformatted(ioinfo.c_str());
                    }
                }
            }

            if (deleteIndex.has_value())
            {
                if (!e.filterChain.removeFilter(*deleteIndex))
                {
                    deleteFailed = true;
                }
            }

            if (deleteFailed)
            {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Cannot delete filter: would invalidate next filter's input type");
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
        if (ImGui::BeginTable("EntitiesTable", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Entity");
            ImGui::TableSetupColumn("Select", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Plot", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
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
                const LayerPtr layer = m_page.entities.at(id).filterChain.output();
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

                // Column 2: Plot button
                ImGui::PushID(id * 2 + 1);
                ImGui::TableSetColumnIndex(2);
                {
                    bool canPlotNow = m_plotter.canPlot();
                    if (!canPlotNow) ImGui::BeginDisabled();
                    if (ImGui::Button("Plot"))
                    {
                        m_plotter.startJobSingle(m_page, id);
                    }
                    if (!canPlotNow) ImGui::EndDisabled();
                }
                ImGui::PopID();

                // Column 3: Duplicate button
                ImGui::PushID(id * 2 + 2);
                ImGui::TableSetColumnIndex(3);
                if (ImGui::Button("Duplicate"))
                {
                    int newId = m_page.duplicateEntity(id);
                    if (newId >= 0)
                    {
                        m_interaction.SelectEntity(newId);
                    }
                }
                ImGui::PopID();

                // Column 4: Visible toggle
                ImGui::PushID(id * 2 + 3);
                ImGui::TableSetColumnIndex(4);
                bool visible = const_cast<Entity &>(m_page.entities.at(id)).visible;
                std::string visLabel = fmt::format("###visible:{}", id);
                if (ImGui::Checkbox(visLabel.c_str(), &visible))
                {
                    const_cast<Entity &>(m_page.entities.at(id)).visible = visible;
                }
                ImGui::PopID();

                // Column 5: Color picker (entity-level color)
                ImGui::PushID(id * 2 + 4);
                ImGui::TableSetColumnIndex(5);
                {
                    Entity &ent = const_cast<Entity &>(m_page.entities.at(id));
                    ImVec4 col(ent.color.r, ent.color.g, ent.color.b, ent.color.a);
                    std::string colLabel = fmt::format("###color:{}", id);
                    if (ImGui::ColorEdit4(colLabel.c_str(), (float *)&col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
                    {
                        ent.color = Color(col.x, col.y, col.z, col.w);
                    }
                }
                ImGui::PopID();

                // Column 6: Delete button
                ImGui::PushID(id * 2 + 5);
                ImGui::TableSetColumnIndex(6);
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
