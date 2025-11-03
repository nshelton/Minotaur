#pragma once

#include <string>
#include "serial/SerialController.h"

struct AxiDrawState {
    int penUpPos{16000};
    int penDownPos{12000};
    int upDownMs{100}; // computed as |up-down| * 0.06
};

class AxiDrawController {
public:
    explicit AxiDrawController(SerialController &serial, AxiDrawState &state)
        : m_serial(serial), m_state(state) {}

    bool initialize(std::string *errorOut = nullptr);

    bool setPenUpValue(int v, std::string *errorOut = nullptr);
    bool setPenDownValue(int v, std::string *errorOut = nullptr);

    bool penUp(int durationMs = -1, std::string *errorOut = nullptr);
    bool penDown(int durationMs = -1, std::string *errorOut = nullptr);

    // EBB motion/control commands
    bool stepperMove(int durationMs, int aSteps, int bSteps, std::string *errorOut = nullptr);
    bool lowLevelMove(int rateStepsPerSecond, int aSteps, int bSteps, std::string *errorOut = nullptr);
    bool enableMotors(bool enable1, bool enable2, std::string *errorOut = nullptr);
    bool disengageMotors(std::string *errorOut = nullptr);
    bool reset(std::string *errorOut = nullptr);

private:
    bool sendCmd(const std::string &cmd, std::string *errorOut);
    void recomputeUpDownMs();

    SerialController &m_serial;
    AxiDrawState &m_state;
};


