#include "plotters/PlotSpooler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

#include <glog/logging.h>
#include "utils/HilbertCurve.h"

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

// Helper: compute centroid of a path
static Vec2 pathCentroid(const Path &p)
{
    if (p.points.empty()) return Vec2(0.0f, 0.0f);
    Vec2 sum(0.0f, 0.0f);
    for (const auto &pt : p.points)
    {
        sum.x += pt.x;
        sum.y += pt.y;
    }
    return sum / static_cast<float>(p.points.size());
}

// Helper: compute bounding box of all paths
static void computePathsBounds(const std::vector<Path> &paths, Vec2 &minOut, Vec2 &maxOut)
{
    if (paths.empty())
    {
        minOut = Vec2(0.0f, 0.0f);
        maxOut = Vec2(0.0f, 0.0f);
        return;
    }

    minOut = Vec2(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
    maxOut = Vec2(-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity());

    for (const auto &path : paths)
    {
        for (const auto &pt : path.points)
        {
            if (pt.x < minOut.x) minOut.x = pt.x;
            if (pt.y < minOut.y) minOut.y = pt.y;
            if (pt.x > maxOut.x) maxOut.x = pt.x;
            if (pt.y > maxOut.y) maxOut.y = pt.y;
        }
    }
}

// Reorder paths using spatial binning with Hilbert curve ordering
// gridSize: number of bins per dimension (e.g., 16 means 16x16 = 256 bins)
static std::vector<Path> reorderPathsHilbert(const std::vector<Path> &in, const Vec2 &start, int gridSize = 16)
{
    if (in.empty()) return {};

    // Ensure gridSize is power of 2 for Hilbert curve
    int n = 1;
    while (n < gridSize) n *= 2;
    gridSize = n;

    // Compute bounding box of all paths
    Vec2 boundsMin, boundsMax;
    computePathsBounds(in, boundsMin, boundsMax);

    // Add small padding to avoid edge cases
    const float padding = 1.0f;
    boundsMin.x -= padding;
    boundsMin.y -= padding;
    boundsMax.x += padding;
    boundsMax.y += padding;

    Vec2 boundsSize = boundsMax - boundsMin;
    if (boundsSize.x < 1e-6f) boundsSize.x = 1.0f;
    if (boundsSize.y < 1e-6f) boundsSize.y = 1.0f;

    // Group paths into bins based on their centroid
    // Map: Hilbert index -> list of path indices
    std::map<uint32_t, std::vector<size_t>> bins;

    for (size_t i = 0; i < in.size(); ++i)
    {
        if (in[i].points.empty()) continue;

        Vec2 centroid = pathCentroid(in[i]);

        // Normalize to [0, gridSize-1]
        int gridX = static_cast<int>((centroid.x - boundsMin.x) / boundsSize.x * gridSize);
        int gridY = static_cast<int>((centroid.y - boundsMin.y) / boundsSize.y * gridSize);

        // Clamp to valid range
        gridX = std::max(0, std::min(gridSize - 1, gridX));
        gridY = std::max(0, std::min(gridSize - 1, gridY));

        // Compute Hilbert index
        uint32_t hilbertIndex = HilbertCurve::xy2d(gridSize, gridX, gridY);

        bins[hilbertIndex].push_back(i);
    }

    // Build output by processing bins in Hilbert order
    std::vector<Path> out;
    out.reserve(in.size());

    Vec2 currentPos = start;

    // Iterate through bins in sorted order (Hilbert curve order)
    for (const auto &binEntry : bins)
    {
        const std::vector<size_t> &pathIndices = binEntry.second;

        // Create a sub-list of paths in this bin
        std::vector<Path> binPaths;
        binPaths.reserve(pathIndices.size());
        for (size_t idx : pathIndices)
        {
            binPaths.push_back(in[idx]);
        }

        // Order paths within the bin using nearest neighbor
        std::vector<Path> orderedBinPaths = reorderPathsNearest(binPaths, currentPos);

        // Append to output
        for (auto &path : orderedBinPaths)
        {
            if (!path.points.empty())
            {
                currentPos = path.points.back();
            }
            out.push_back(std::move(path));
        }
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
    m_onlyEntityId.reset();

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

bool PlotSpooler::startJobSingle(const PageModel &page, int entityId, const PlotterConfig &cfg, bool liftPen)
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
    m_onlyEntityId = entityId;

    if (!prepareJob(page, liftPen))
    {
        LOG(WARNING) << "PlotSpooler: nothing to plot for entity " << entityId;
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        // Prime the queue to a reasonable high-water mark
        refillQueueLocked(/*highWaterMs=*/1200, /*lowWaterMs=*/300);
    }

    m_startTime = Clock::now();
    if (m_worker.joinable())
    {
        m_worker.join();
    }

    m_cancel.store(false);
    m_paused.store(false);
    m_running.store(true);

    m_worker = std::thread([this]() { run(); });
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

std::vector<Path> PlotSpooler::getRemainingPaths() const
{
    std::lock_guard<std::mutex> lk(m_mutex);

    if (!m_job.prepared || m_job.pathIndex >= m_job.orderedPaths.size())
    {
        return {};
    }

    // Return paths from current index to end
    std::vector<Path> remaining;
    remaining.reserve(m_job.orderedPaths.size() - m_job.pathIndex);
    for (size_t i = m_job.pathIndex; i < m_job.orderedPaths.size(); ++i)
    {
        remaining.push_back(m_job.orderedPaths[i]);
    }
    return remaining;
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
        if (m_onlyEntityId.has_value() && kv.first != *m_onlyEntityId)
            continue;
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
        if (m_onlyEntityId.has_value() && kv.first != *m_onlyEntityId)
            continue;
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
    m_job.orderedPaths = reorderPathsHilbert(pagePaths, currentPosMm, 16);
    m_job.prepared = true;

    m_stats.plannedPenDownMm = totalDrawn;
    m_stats.donePenDownMm = 0.0f;
    m_stats.queuedMs = 0;
    m_stats.currentPathIndex = 0;
    m_stats.totalPaths = static_cast<int>(m_job.orderedPaths.size());
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
                auto result = planPath(s, backPts, /*penUp=*/true, m_job.currentPosMm);
                for (const auto &mv : result.moves)
                {
                    pushSM(mv.dtMs, mv.aSteps, mv.bSteps);
                }
                m_job.currentPosMm = result.finalPositionMm;
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
                auto result = planPath(s, travelPts, /*penUp=*/true, m_job.currentPosMm);
                for (const auto &mv : result.moves)
                {
                    pushSM(mv.dtMs, mv.aSteps, mv.bSteps);
                }
                m_job.currentPosMm = result.finalPositionMm;
            }
            if (m_job.liftPen)
                pushPenDown();

            auto result = planPath(s, path.points, /*penUp=*/false, m_job.currentPosMm); m_job.activeMoves = result.moves; m_job.activePathFinalPosMm = result.finalPositionMm;
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
            m_job.currentPosMm = m_job.activePathFinalPosMm;
            m_job.drawingPhase = false;
            m_job.activeMoves.clear();
            m_job.moveIndex = 0;
            m_job.pathIndex++;
            m_stats.currentPathIndex = m_job.pathIndex;
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
        int sleepDurationMs = 0;

        switch (cmd.kind)
        {
        case CmdKind::PenUp:
            ok = m_axidraw.penUp(-1, &err);
            penDownActive = false;
            sleepDurationMs = m_axidraw.getState().upDownMs;
            break;
        case CmdKind::PenDown:
            ok = m_axidraw.penDown(-1, &err);
            penDownActive = true;
            sleepDurationMs = m_axidraw.getState().upDownMs;
            break;
        case CmdKind::StepperMove:
            ok = m_axidraw.stepperMove(cmd.durationMs, cmd.aSteps, cmd.bSteps, &err);
            sleepDurationMs = cmd.durationMs;
            break;
        }

        if (!ok)
        {
            LOG(ERROR) << "Command failed: " << err;
            break;
        }

        m_stats.commandsSent++;

        // Process stepper move stats and queue refill
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
        }

        // Wait for command to complete (works for all command types)
        std::this_thread::sleep_for(Ms(std::max(1, sleepDurationMs)));
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
