#include "MainScreen.h"
#include "utils/PathSetGenerator.h"
#include "utils/BitmapGenerator.h"
#include <string>
#include <fmt/format.h>
#include <ctime>
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

        if (ImGui::Button("Add Date/Time Text"))
        {
            std::time_t now = std::time(nullptr);
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &now);
#else
            tm = *std::localtime(&now);
#endif
            std::string dt = fmt::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}",
                                         tm.tm_year + 1900,
                                         tm.tm_mon + 1,
                                         tm.tm_mday,
                                         tm.tm_hour,
                                         tm.tm_min,
                                         tm.tm_sec);

            auto ps = PathSetGenerator::Text(dt, center, 12.0f, 2.0f, theme::PathsetColor);
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

        ImGui::Separator();
        ImGui::Text("Plotter");
        ImGui::InputText("Port", m_portBuf, sizeof(m_portBuf));
        bool connected = m_serial.isConnected();
        ImGui::Text("Connected: %s", connected ? "Yes" : "No");
        if (!m_serial.state().lastError.empty())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Last error: %s", m_serial.state().lastError.c_str());
        }

        if (!connected)
        {
            ImGui::SameLine();
            if (ImGui::Button("Auto Connect"))
            {
                std::string err;
                std::string chosen;
                if (m_serial.autoConnect(&chosen, 115200, &err))
                {
                    strncpy(m_portBuf, chosen.c_str(), sizeof(m_portBuf) - 1);
                    m_portBuf[sizeof(m_portBuf) - 1] = '\0';
                    m_ax = std::make_unique<AxiDrawController>(m_serial, m_axState);
                    std::string ierr;
                    m_ax->initialize(&ierr);
                }
            }
        }
        else
        {
            if (ImGui::Button("Disconnect"))
            {
                m_ax.reset();
                m_serial.disconnect();
            }

            ImGui::SameLine();
            if (ImGui::Button("Pen Up"))
            {
                if (m_ax)
                {
                    std::string err;
                    m_ax->penUp(-1, &err);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Pen Down"))
            {
                if (m_ax)
                {
                    std::string err;
                    m_ax->penDown(-1, &err);
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Disengage Motors"))
            {
                if (m_ax)
                {
                    std::string err;
                    m_ax->disengageMotors(&err);
                }
            }

            // Pen position sliders
            ImGui::Separator();
            int up = m_axState.penUpPos;
            int down = m_axState.penDownPos;
            if (ImGui::SliderInt("Pen Up Position", &up, 8000, 20000))
            {
                m_axState.penUpPos = up;
                m_plotter.penUpPos = up;
                if (m_ax)
                {
                    std::string e;
                    m_ax->setPenUpValue(up, &e);
                }
            }
            if (ImGui::SliderInt("Pen Down Position", &down, 8000, 20000))
            {
                m_axState.penDownPos = down;
                m_plotter.penDownPos = down;
                if (m_ax)
                {
                    std::string e;
                    m_ax->setPenDownValue(down, &e);
                }
            }

            // Speed sliders
            ImGui::Separator();
            int drawPct = m_plotter.drawSpeedPercent;
            int travelPct = m_plotter.travelSpeedPercent;
            if (ImGui::SliderInt("Drawing Speed (%)", &drawPct, 10, 300))
            {
                m_plotter.drawSpeedPercent = drawPct;
                if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_plotter);
            }
            if (ImGui::SliderInt("Travel Speed (%)", &travelPct, 10, 300))
            {
                m_plotter.travelSpeedPercent = travelPct;
                if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_plotter);
            }

            if (ImGui::CollapsingHeader("Advanced Motion", ImGuiTreeNodeFlags_DefaultOpen))
            {
                float drawSpeed = m_plotter.drawSpeedMmPerS;
                float travelSpeed = m_plotter.travelSpeedMmPerS;
                float accelDraw = m_plotter.accelDrawMmPerS2;
                float accelTravel = m_plotter.accelTravelMmPerS2;
                float cornering = m_plotter.cornering;
                int junctionFloor = m_plotter.junctionSpeedFloorPercent;
                int sliceMs = m_plotter.timeSliceMs;
                int maxRate = m_plotter.maxStepRatePerAxis;
                float minSeg = m_plotter.minSegmentMm;

                if (ImGui::SliderFloat("Draw Speed (mm/s)", &drawSpeed, 5.0f, 200.0f, "%.1f"))
                { m_plotter.drawSpeedMmPerS = drawSpeed; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_plotter); }
                if (ImGui::SliderFloat("Travel Speed (mm/s)", &travelSpeed, 5.0f, 300.0f, "%.1f"))
                { m_plotter.travelSpeedMmPerS = travelSpeed; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_plotter); }
                if (ImGui::SliderFloat("Draw Accel (mm/s^2)", &accelDraw, 50.0f, 5000.0f, "%.0f"))
                { m_plotter.accelDrawMmPerS2 = accelDraw; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_plotter); }
                if (ImGui::SliderFloat("Travel Accel (mm/s^2)", &accelTravel, 50.0f, 8000.0f, "%.0f"))
                { m_plotter.accelTravelMmPerS2 = accelTravel; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_plotter); }
                if (ImGui::SliderFloat("Cornering (jd, mm)", &cornering, 0.00f, 2.00f, "%.2f"))
                { m_plotter.cornering = cornering; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_plotter); }
                if (ImGui::SliderInt("Junction Speed Floor (%)", &junctionFloor, 0, 100))
                { m_plotter.junctionSpeedFloorPercent = junctionFloor; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_plotter); }
                if (ImGui::SliderInt("Time Slice (ms)", &sliceMs, 2, 100))
                { m_plotter.timeSliceMs = sliceMs; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_plotter); }
                if (ImGui::SliderInt("Max Step Rate (steps/s)", &maxRate, 100, 3000))
                { m_plotter.maxStepRatePerAxis = maxRate; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_plotter); }
                if (ImGui::SliderFloat("Min Segment (mm)", &minSeg, 0.01f, 1.0f, "%.2f"))
                { m_plotter.minSegmentMm = minSeg; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_plotter); }
            }

            ImGui::Separator();
            ImGui::Text("Job");
            bool spoolerRunning = (m_spooler && m_spooler->isRunning());
            if (!spoolerRunning)
            {
                if (ImGui::Button("Start Plot"))
                {
                    if (m_ax)
                    {
                        if (!m_spooler)
                        {
                            m_spooler = std::make_unique<PlotSpooler>(m_serial, *m_ax);
                        }
                        if (m_spooler->startJob(m_page, m_plotter, /*liftPen=*/true))
                        {
                            m_showPlotProgress = true;
                        }
                    }
                }
            }
            else
            {
                PlotSpooler::Stats s = m_spooler->stats();
                ImGui::Text("Queued: %d  Sent: %d  Plotted(mm): %.1f / %.1f", s.commandsQueued, s.commandsSent, s.donePenDownMm, s.plannedPenDownMm);
                float frac = s.percentComplete;
                if (s.plannedPenDownMm <= 0.0f) {
                    frac = 0.0f;
                }
                if (frac < 0.0f) frac = 0.0f; if (frac > 1.0f) frac = 1.0f;
                ImGui::ProgressBar(frac, ImVec2(-1, 0), nullptr);
                // Elapsed and ETA display
                int elapsedSec = s.elapsedMs / 1000;
                int etaSec = s.etaMs / 1000;
                int eMin = elapsedSec / 60, eSec = elapsedSec % 60;
                int rMin = etaSec / 60, rSec = etaSec % 60;
                ImGui::Text("Elapsed: %02d:%02d   ETA: %02d:%02d   %.0f%%", eMin, eSec, rMin, rSec, frac * 100.0f);
                if (!m_spooler->isPaused())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Pause"))
                    {
                        m_spooler->pause();
                    }
                }
                else
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Resume"))
                    {
                        m_spooler->resume();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    m_spooler->cancel();
                    m_showPlotProgress = false;
                }
            }
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
            // ImGui::Text("Payload Version: %llu", static_cast<unsigned long long>(e.payloadVersion));
            auto kindToString = [](LayerKind k) {
                return (k == LayerKind::Bitmap) ? "Bitmap" : (k == LayerKind::PathSet ? "PathSet" : "Float");
            };
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

                        float val = param.value;
                        if (ImGui::SliderFloat(label.c_str(), &val, param.minValue, param.maxValue))
                        {
                            f->setParameter(paramKey, val);
                        }
                    }

                    if (f->outputKind() == LayerKind::PathSet)
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

                // Column 2: Plot button
                ImGui::PushID(id * 2 + 1);
                ImGui::TableSetColumnIndex(2);
                {
                    bool spoolerRunning = (m_spooler && m_spooler->isRunning());
                    bool canPlot = (m_ax != nullptr) && m_serial.isConnected() && !spoolerRunning;
                    if (!canPlot) ImGui::BeginDisabled();
                    if (ImGui::Button("Plot"))
                    {
                        if (m_ax)
                        {
                            if (!m_spooler)
                            {
                                m_spooler = std::make_unique<PlotSpooler>(m_serial, *m_ax);
                            }
                            if (m_spooler->startJobSingle(m_page, id, m_plotter, /*liftPen=*/true))
                            {
                                m_showPlotProgress = true;
                            }
                        }
                    }
                    if (!canPlot) ImGui::EndDisabled();
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
