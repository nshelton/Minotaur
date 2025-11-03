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

static inline float dist2(const Vec2 &a, const Vec2 &b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

static inline void reversePathPoints(std::vector<Vec2> &pts)
{
    std::reverse(pts.begin(), pts.end());
}

// Reorder a set of page-space paths by greedy nearest neighbor, allowing flips for open paths
static std::vector<Path> reorderPathsNearest(const std::vector<Path> &in, const Vec2 &start)
{
    const size_t n = in.size();
    std::vector<bool> used(n, false);
    std::vector<Path> out;
    out.reserve(n);

    Vec2 last = start;

    for (size_t k = 0; k < n; ++k)
    {
        size_t bestIdx = static_cast<size_t>(-1);
        bool bestFlip = false;
        float bestD2 = std::numeric_limits<float>::infinity();

        for (size_t i = 0; i < n; ++i)
        {
            if (used[i]) continue;
            const Path &p = in[i];
            if (p.points.empty()) continue;

            float dStart = dist2(last, p.points.front());
            float dEnd = dist2(last, p.points.back());
            float cand = dStart;
            bool candFlip = false;
            if (!p.closed && dEnd < dStart) { cand = dEnd; candFlip = true; }

            if (cand < bestD2)
            {
                bestD2 = cand;
                bestIdx = i;
                bestFlip = candFlip;
            }
        }

        if (bestIdx == static_cast<size_t>(-1))
            break;

        Path chosen = in[bestIdx];
        used[bestIdx] = true;
        if (bestFlip)
            reversePathPoints(chosen.points);
        if (!chosen.points.empty())
            last = chosen.points.back();
        out.push_back(std::move(chosen));
    }

    return out;
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
        m_queuedMs = 0;
        m_job = JobState{};
        std::queue<Cmd> empty;
        std::swap(m_queue, empty);
    }

    m_cfg = cfg;

    if (!prepareJob(page, liftPen))
    {
        LOG(WARNING) << "PlotSpooler: nothing to plot";
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        // Prime the queue to a reasonable high-water mark
        refillQueueLocked(/*highWaterMs=*/1200, /*lowWaterMs=*/300);
    }

    m_startTime = Clock::now();

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

void PlotSpooler::updateConfig(const PlotterConfig &cfg)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_cfg = cfg;
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
    m_stats.queuedMs += std::max(1, durationMs);
    m_queuedMs += std::max(1, durationMs);
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

    m_stats.plannedPenDownMm = totalDrawn;
    return totalSegments > 0;
}

bool PlotSpooler::buildQueuePlanned(const PageModel &page, bool liftPen)
{
    // Deprecated in favor of short-queue refill strategy. Keeping for reference.
    // Build nothing here to avoid filling long queues.
    (void)page;
    (void)liftPen;
    return false;
}

bool PlotSpooler::prepareJob(const PageModel &page, bool liftPen)
{
    // Transform to page space and reorder paths, compute total pen-down mm
    std::vector<Path> pagePaths;
    Vec2 currentPosMm = Vec2(0.0f, 0.0f);
    float totalDrawn = 0.0f;

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
        if (!ps) continue;

        for (const Path &path : ps->paths)
        {
            if (path.points.size() < 1) continue;
            Path pspace;
            pspace.closed = path.closed;
            pspace.points.reserve(path.points.size());
            for (const Vec2 &p : path.points)
                pspace.points.push_back(e.localToPage * p);
            // Accumulate length
            for (size_t i = 1; i < pspace.points.size(); ++i)
            {
                float dx = pspace.points[i].x - pspace.points[i - 1].x;
                float dy = pspace.points[i].y - pspace.points[i - 1].y;
                totalDrawn += std::hypot(dx, dy);
            }
            pagePaths.push_back(std::move(pspace));
        }
    }

    if (pagePaths.empty())
        return false;

    m_job = JobState{};
    m_job.liftPen = liftPen;
    m_job.currentPosMm = Vec2(0.0f, 0.0f);
    m_job.orderedPaths = reorderPathsNearest(pagePaths, currentPosMm);
    m_job.prepared = true;

    m_stats.plannedPenDownMm = totalDrawn;
    m_stats.donePenDownMm = 0.0f;
    m_stats.queuedMs = 0;
    m_queuedMs = 0;
    return true;
}

void PlotSpooler::refillQueueLocked(int highWaterMs, int lowWaterMs)
{
    (void)lowWaterMs; // currently unused; kept for future logic
    if (!m_job.prepared)
        return;

    // Recompute planner settings from current config to reflect live updates
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

    // Ensure initial pen-up once if requested
    if (m_job.liftPen && !m_job.sentInitialPenUp)
    {
        pushPenUp();
        m_job.sentInitialPenUp = true;
    }

    while (m_queuedMs < highWaterMs)
    {
        // If all paths processed, return home once
        if (m_job.pathIndex >= m_job.orderedPaths.size())
        {
            if (!m_job.returnedHome)
            {
                std::vector<Vec2> backPts{ m_job.currentPosMm, Vec2(0.0f, 0.0f) };
                auto backMoves = planPath(s, backPts, /*penUp=*/true, m_job.currentPosMm);
                for (const auto &mv : backMoves)
                {
                    pushSM(mv.dtMs, mv.aSteps, mv.bSteps);
                }
                m_job.currentPosMm = Vec2(0.0f, 0.0f);
                m_job.returnedHome = true;
            }
            break; // nothing more to enqueue
        }

        Path &path = m_job.orderedPaths[m_job.pathIndex];
        if (path.points.empty())
        {
            m_job.pathIndex++;
            continue;
        }

        // If not currently drawing, travel to start and (optionally) pen down, then plan draw moves
        if (!m_job.drawingPhase)
        {
            if (dist2(m_job.currentPosMm, path.points.front()) > 0.0f)
            {
                std::vector<Vec2> travelPts{ m_job.currentPosMm, path.points.front() };
                auto travelMoves = planPath(s, travelPts, /*penUp=*/true, m_job.currentPosMm);
                for (const auto &mv : travelMoves)
                {
                    pushSM(mv.dtMs, mv.aSteps, mv.bSteps);
                }
                m_job.currentPosMm = path.points.front();
            }
            if (m_job.liftPen)
                pushPenDown();

            m_job.activeMoves = planPath(s, path.points, /*penUp=*/false, m_job.currentPosMm);
            m_job.moveIndex = 0;
            m_job.drawingPhase = true;
        }

        // Add draw moves up to high-water mark
        while (m_job.moveIndex < m_job.activeMoves.size() && m_queuedMs < highWaterMs)
        {
            const auto &mv = m_job.activeMoves[m_job.moveIndex++];
            pushSM(mv.dtMs, mv.aSteps, mv.bSteps);
        }

        // If finished current path, pen up, advance to next
        if (m_job.moveIndex >= m_job.activeMoves.size())
        {
            if (m_job.liftPen)
                pushPenUp();
            m_job.currentPosMm = path.points.back();
            m_job.drawingPhase = false;
            m_job.activeMoves.clear();
            m_job.moveIndex = 0;
            m_job.pathIndex++;
        }
        else
        {
            // Reached high water, exit to let worker drain
            break;
        }
    }
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

    bool penDownActive = false;
    const int kLowWaterMs = 300;
    const int kHighWaterMs = 1200;

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
            {
                // Try to refill when empty
                refillQueueLocked(kHighWaterMs, kLowWaterMs);
                if (m_queue.empty())
                {
                    // Nothing more to do
                    break;
                }
            }
            cmd = m_queue.front();
            m_queue.pop();
        }

        bool ok = true;
        switch (cmd.kind)
        {
        case CmdKind::PenUp:
            ok = m_axidraw.penUp(-1, &err);
            penDownActive = false;
            break;
        case CmdKind::PenDown:
            ok = m_axidraw.penDown(-1, &err);
            penDownActive = true;
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
            // Convert CoreXY steps to mm for progress if pen is down
            if (penDownActive)
            {
                const float a = static_cast<float>(cmd.aSteps);
                const float b = static_cast<float>(cmd.bSteps);
                const float dxSteps = 0.5f * (a - b);
                const float dySteps = 0.5f * (a + b);
                const float dxMm = dxSteps / static_cast<float>(kStepsPerMm);
                const float dyMm = dySteps / static_cast<float>(kStepsPerMm);
                m_stats.donePenDownMm += std::hypot(dxMm, dyMm);
            }

            // Decrease queued time and top up if needed
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                const int dt = std::max(1, cmd.durationMs);
                m_queuedMs = std::max(0, m_queuedMs - dt);
                m_stats.queuedMs = std::max(0, m_stats.queuedMs - dt);
                // Update time and ETA
                auto now = Clock::now();
                m_stats.elapsedMs = static_cast<int>(std::chrono::duration_cast<Ms>(now - m_startTime).count());
                float frac = 0.0f;
                if (m_stats.plannedPenDownMm > 0.0f)
                {
                    frac = m_stats.donePenDownMm / m_stats.plannedPenDownMm;
                    if (frac < 0.0f) frac = 0.0f;
                    if (frac > 1.0f) frac = 1.0f;
                }
                m_stats.percentComplete = frac;
                if (frac > 0.0f)
                {
                    const float totalMs = static_cast<float>(m_stats.elapsedMs) / frac;
                    int eta = static_cast<int>(std::max(0.0f, totalMs - static_cast<float>(m_stats.elapsedMs)));
                    m_stats.etaMs = eta;
                }
                else
                {
                    m_stats.etaMs = 0;
                }
                if (m_queuedMs < kLowWaterMs)
                {
                    refillQueueLocked(kHighWaterMs, kLowWaterMs);
                }
            }

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
