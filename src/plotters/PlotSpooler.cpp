#include "plotters/PlotSpooler.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <glog/logging.h>

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::milliseconds;

static inline float hypot2f(float dx, float dy)
{
    return std::sqrt(dx * dx + dy * dy);
}

PlotSpooler::~PlotSpooler()
{
    // Ensure background thread is stopped before destruction
    cancel();
    if (m_worker.joinable())
    {
        m_worker.join();
    }
}

bool PlotSpooler::startJob(const PageModel &page, const PlotterConfig &cfg, bool liftPen)
{
    if (m_running.load())
    {
        LOG(WARNING) << "PlotSpooler already running";
        return false;
    }
    if (!m_serial.isConnected())
    {
        LOG(WARNING) << "Serial not connected; cannot start plot job";
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stats = Stats{};
        std::queue<Cmd> empty;
        std::swap(m_queue, empty);
    }

    m_cfg = cfg;

    if (!buildQueuePlanned(page, liftPen))
    {
        LOG(WARNING) << "PlotSpooler: nothing to plot";
        return false;
    }

    // Join any previous finished worker to avoid std::terminate on reassignment
    if (m_worker.joinable())
    {
        m_worker.join();
    }

    m_cancel.store(false);
    m_paused.store(false);
    m_running.store(true);

    m_worker = std::thread([this]()
                           { run(); });
    return true;
}

void PlotSpooler::pause()
{
    m_paused.store(true);
}

void PlotSpooler::resume()
{
    if (m_paused.exchange(false))
    {
        m_cv.notify_all();
    }
}

void PlotSpooler::cancel()
{
    m_cancel.store(true);
    m_cv.notify_all();
    if (m_worker.joinable())
    {
        m_worker.join();
    }
}

void PlotSpooler::mmDeltaToCoreXYSteps(float dxMm, float dyMm, int &aOut, int &bOut)
{
    const int dxSteps = roundToInt(dxMm * static_cast<float>(kStepsPerMm));
    const int dySteps = roundToInt(dyMm * static_cast<float>(kStepsPerMm));
    const int a = dxSteps + dySteps;
    const int b = -dxSteps + dySteps;
    aOut = a;
    bOut = b;
}

int PlotSpooler::computeDurationMsForAB(int aSteps, int bSteps, bool plotting) const
{
    const int distanceSteps = std::max(std::abs(aSteps), std::abs(bSteps));
    const int pctInt = plotting ? m_cfg.drawSpeedPercent : m_cfg.travelSpeedPercent;
    const float pct = static_cast<float>(std::clamp(pctInt, 1, 500)) / 100.0f;
    const float stepsPerSecond = std::max(1.0f, pct * static_cast<float>(kMaxStepsPerSecond));
    const float ms = 1000.0f * (static_cast<float>(distanceSteps) / stepsPerSecond);
    const int dur = std::max(1, roundToInt(ms));
    return dur;
}

void PlotSpooler::pushPenUp()
{
    Cmd c;
    c.kind = CmdKind::PenUp;
    m_queue.push(c);
    m_stats.commandsQueued++;
}

void PlotSpooler::pushPenDown()
{
    Cmd c;
    c.kind = CmdKind::PenDown;
    m_queue.push(c);
    m_stats.commandsQueued++;
}

void PlotSpooler::pushSM(int durationMs, int aSteps, int bSteps)
{
    Cmd c;
    c.kind = CmdKind::StepperMove;
    c.durationMs = durationMs;
    c.aSteps = aSteps;
    c.bSteps = bSteps;
    m_queue.push(c);
    m_stats.commandsQueued++;
}

bool PlotSpooler::buildQueue(const PageModel &page, bool liftPen)
{
    // Collect all transformed paths
    int totalSegments = 0;
    float totalDrawn = 0.0f;

    // Ensure pen up initially if requested
    if (liftPen)
    {
        pushPenUp();
    }

    Vec2 currentPosMm = Vec2(0.0f, 0.0f);

    for (const auto &kv : page.entities)
    {
        const Entity &e = kv.second;
        if (e.type() != EntityType::PathSet)
            continue;
        const PathSet *ps = e.pathset();
        if (!ps)
            continue;

        for (const Path &path : ps->paths)
        {
            if (path.points.size() < 1)
                continue;

            // Transform first point to page space
            Vec2 p0 = e.localToPage * path.points[0];

            // Travel move to start (pen up)
            int a = 0, b = 0;
            mmDeltaToCoreXYSteps(p0.x - currentPosMm.x, p0.y - currentPosMm.y, a, b);
            const int durTravel = computeDurationMsForAB(a, b, /*plotting=*/false);
            pushSM(durTravel, a, b);
            currentPosMm = p0;
            totalSegments++;

            if (liftPen)
            {
                pushPenDown();
            }

            // Draw segments
            for (size_t i = 1; i < path.points.size(); ++i)
            {
                Vec2 p = e.localToPage * path.points[i];
                const float dx = p.x - currentPosMm.x;
                const float dy = p.y - currentPosMm.y;
                mmDeltaToCoreXYSteps(dx, dy, a, b);
                const int durPlot = computeDurationMsForAB(a, b, /*plotting=*/true);
                pushSM(durPlot, a, b);
                currentPosMm = p;
                totalSegments++;
                totalDrawn += hypot2f(dx, dy);
            }

            if (liftPen)
            {
                pushPenUp();
            }
        }
    }

    // Return to origin after all moves (non-plotting)
    if (totalSegments > 0)
    {
        int a = 0, b = 0;
        mmDeltaToCoreXYSteps(-currentPosMm.x, -currentPosMm.y, a, b);
        const int durBack = computeDurationMsForAB(a, b, /*plotting=*/false);
        pushSM(durBack, a, b);
        totalSegments++;
    }

    m_stats.penDownDistanceMm = totalDrawn;
    return totalSegments > 0;
}

bool PlotSpooler::buildQueuePlanned(const PageModel &page, bool liftPen)
{
    // Planner settings derived from config
    PlannerSettings s;
    s.speedPenDownMmPerS = m_cfg.drawSpeedMmPerS;
    s.speedPenUpMmPerS = m_cfg.travelSpeedMmPerS;
    s.accelPenDownMmPerS2 = m_cfg.accelDrawMmPerS2;
    s.accelPenUpMmPerS2 = m_cfg.accelTravelMmPerS2;
    s.cornering = m_cfg.cornering;
    s.junctionSpeedFloorPercent = m_cfg.junctionSpeedFloorPercent;
    s.timeSliceMs = m_cfg.timeSliceMs;
    s.maxStepRatePerAxis = m_cfg.maxStepRatePerAxis;
    s.minSegmentMm = m_cfg.minSegmentMm;
    s.stepsPerMm = kStepsPerMm;

    int totalCmds = 0;
    float totalDrawn = 0.0f;

    Vec2 currentPosMm = Vec2(0.0f, 0.0f);

    if (liftPen)
        pushPenUp();

    for (const auto &kv : page.entities)
    {
        const Entity &e = kv.second;
        const PathSet *ps = nullptr;

        if (e.type() == EntityType::PathSet)
        {
            ps = e.pathset();
        }
        else if (e.filterChain.outputLayer()->kind() == LayerKind::PathSet)
        {
            const auto &output = e.filterChain.outputLayer();
            ps = asPathSetConstPtr(output);
        }

        if (!ps)
            continue;

        for (const Path &path : ps->paths)
        {
            if (path.points.size() < 1)
                continue;

            // Transform points to page space
            std::vector<Vec2> pts;
            pts.reserve(path.points.size());
            for (const Vec2 &p : path.points)
                pts.push_back(e.localToPage * p);

            // Travel to start
            std::vector<Vec2> travelPts;
            travelPts.push_back(currentPosMm);
            travelPts.push_back(pts.front());
            auto travelMoves = planPath(s, travelPts, /*penUp=*/true, currentPosMm);
            for (const auto &mv : travelMoves)
            {
                pushSM(mv.dtMs, mv.aSteps, mv.bSteps);
                ++totalCmds;
            }
            currentPosMm = pts.front();

            if (liftPen)
                pushPenDown();

            // Draw path
            auto drawMoves = planPath(s, pts, /*penUp=*/false, currentPosMm);
            for (const auto &mv : drawMoves)
            {
                pushSM(mv.dtMs, mv.aSteps, mv.bSteps);
                ++totalCmds;
            }
            // Update current position by summing deltas
            for (size_t i = 1; i < pts.size(); ++i)
            {
                float dx = pts[i].x - pts[i - 1].x;
                float dy = pts[i].y - pts[i - 1].y;
                totalDrawn += std::hypot(dx, dy);
            }
            currentPosMm = pts.back();

            if (liftPen)
                pushPenUp();
        }
    }

    // Return to origin
    if (totalCmds > 0)
    {
        std::vector<Vec2> backPts;
        backPts.push_back(currentPosMm);
        backPts.push_back(Vec2(0.0f, 0.0f));
        auto backMoves = planPath(s, backPts, /*penUp=*/true, currentPosMm);
        for (const auto &mv : backMoves)
        {
            pushSM(mv.dtMs, mv.aSteps, mv.bSteps);
            ++totalCmds;
        }
    }

    m_stats.penDownDistanceMm = totalDrawn;
    return totalCmds > 0;
}

void PlotSpooler::run()
{
    LOG(INFO) << "PlotSpooler worker started";

    // Try to configure servo on start (safe if already configured)
    std::string err;
    if (!m_axidraw.initialize(&err))
    {
        LOG(WARNING) << "AxiDraw initialize failed: " << err;
    }

    // Enable motors
    if (!m_axidraw.enableMotors(true, true, &err))
    {
        LOG(WARNING) << "Failed to enable motors: " << err;
    }

    while (!m_cancel.load())
    {
        // Pause handling
        if (m_paused.load())
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [&]()
                      { return !m_paused.load() || m_cancel.load(); });
            if (m_cancel.load())
                break;
        }

        Cmd cmd;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (m_queue.empty())
                break;
            cmd = m_queue.front();
            m_queue.pop();
        }

        bool ok = true;
        switch (cmd.kind)
        {
        case CmdKind::PenUp:
            ok = m_axidraw.penUp(-1, &err);
            break;
        case CmdKind::PenDown:
            ok = m_axidraw.penDown(-1, &err);
            break;
        case CmdKind::StepperMove:
            ok = m_axidraw.stepperMove(cmd.durationMs, cmd.aSteps, cmd.bSteps, &err);
            break;
        }

        if (!ok)
        {
            LOG(ERROR) << "Command failed: " << err;
            break;
        }

        m_stats.commandsSent++;

        // Sleep exact dtMs for SM slices; brief delay for pen toggles
        if (cmd.kind == CmdKind::StepperMove)
        {
            std::this_thread::sleep_for(Ms(std::max(1, cmd.durationMs)));
        }
        else
        {
            std::this_thread::sleep_for(Ms(5));
        }
    }

    // Best-effort pen up and motors off at end (unless cancelled early)
    if (!m_cancel.load())
    {
        (void)m_axidraw.penUp(-1, nullptr);
    }
    (void)m_axidraw.enableMotors(false, false, nullptr);

    m_running.store(false);
    LOG(INFO) << "PlotSpooler worker finished";
}
