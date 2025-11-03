#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "Page.h"
#include "core/Vec2.h"
#include "plotters/AxidrawController.h"
#include "plotters/PlotterConfig.h"
#include "plotters/MotionPlanner.h"
#include "serial/SerialController.h"

// Streams page geometry to AxiDraw using simple SM moves and pen commands.
class PlotSpooler {
public:
    struct Stats {
        int commandsQueued{0};
        int commandsSent{0};
        // Planned total pen-down length (mm)
        float plannedPenDownMm{0.0f};
        // Executed pen-down length (mm) so far
        float donePenDownMm{0.0f};
        // Approximate milliseconds of stepper slices currently queued
        int queuedMs{0};
        // Elapsed time since job start (ms)
        int elapsedMs{0};
        // Percent completion [0..1]
        float percentComplete{0.0f};
        // Estimated time remaining (ms)
        int etaMs{0};
    };

    PlotSpooler(SerialController &serial, AxiDrawController &axidraw)
        : m_serial(serial), m_axidraw(axidraw) {}
    ~PlotSpooler();

    bool startJob(const PageModel &page, const PlotterConfig &cfg, bool liftPen = true);
    void pause();
    void resume();
    void cancel();

    // Update motion parameters for future queued chunks while a job is running
    void updateConfig(const PlotterConfig &cfg);

    bool isRunning() const { return m_running.load(); }
    bool isPaused() const { return m_paused.load(); }
    Stats stats() const { return m_stats; }

private:
    // Short-queue job preparation and refilling
    struct JobState {
        std::vector<Path> orderedPaths;   // page-space, reordered
        size_t pathIndex{0};              // current path
        std::vector<MoveSlice> activeMoves; // planned move slices for current phase
        size_t moveIndex{0};              // index into activeMoves
        bool drawingPhase{false};         // false: need travel; true: drawing
        bool prepared{false};
        bool sentInitialPenUp{false};
        bool returnedHome{false};
        Vec2 currentPosMm{0.0f, 0.0f};
        bool liftPen{true};
    };

    enum class CmdKind { PenUp, PenDown, StepperMove };
    struct Cmd {
        CmdKind kind{CmdKind::StepperMove};
        // For SM: duration ms and motor A/B steps (CoreXY mapped)
        int durationMs{0};
        int aSteps{0};
        int bSteps{0};
    };

    // Constants
    static constexpr int kStepsPerMm = 80; // 2032 steps/in
    static constexpr int kMaxStepsPerSecond = 5000; // conservative streaming speed

    SerialController &m_serial;
    AxiDrawController &m_axidraw;
    PlotterConfig m_cfg{};

    std::thread m_worker{};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_cancel{false};

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<Cmd> m_queue;

    Stats m_stats{};
    int m_queuedMs{0};
    JobState m_job{};
    std::chrono::steady_clock::time_point m_startTime{};

    static inline int roundToInt(float v) { return static_cast<int>(v >= 0.0f ? v + 0.5f : v - 0.5f); }

    // Build command queue from page paths
    bool buildQueue(const PageModel &page, bool liftPen);
    bool buildQueuePlanned(const PageModel &page, bool liftPen);


    bool prepareJob(const PageModel &page, bool liftPen);
    void refillQueueLocked(int highWaterMs, int lowWaterMs);

    // Push helpers
    void pushPenUp();
    void pushPenDown();
    void pushSM(int durationMs, int aSteps, int bSteps);

    // Geometry helpers
    static inline void mmDeltaToCoreXYSteps(float dxMm, float dyMm, int &aOut, int &bOut);
    int computeDurationMsForAB(int aSteps, int bSteps, bool plotting) const;

    // Worker loop
    void run();
};


