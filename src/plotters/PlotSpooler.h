#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "Page.h"
#include "core/Vec2.h"
#include "plotters/AxidrawController.h"
#include "plotters/PlotterConfig.h"
#include "serial/SerialController.h"

// Streams page geometry to AxiDraw using simple SM moves and pen commands.
class PlotSpooler {
public:
    struct Stats {
        int commandsQueued{0};
        int commandsSent{0};
        float penDownDistanceMm{0.0f};
    };

    PlotSpooler(SerialController &serial, AxiDrawController &axidraw)
        : m_serial(serial), m_axidraw(axidraw) {}
    ~PlotSpooler();

    bool startJob(const PageModel &page, const PlotterConfig &cfg, bool liftPen = true);
    void pause();
    void resume();
    void cancel();

    bool isRunning() const { return m_running.load(); }
    bool isPaused() const { return m_paused.load(); }
    Stats stats() const { return m_stats; }

private:
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

    static inline int roundToInt(float v) { return static_cast<int>(v >= 0.0f ? v + 0.5f : v - 0.5f); }

    // Build command queue from page paths
    bool buildQueue(const PageModel &page, bool liftPen);

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


