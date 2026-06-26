#include "plotters/AxidrawController.h"

#include <cmath>
#include <glog/logging.h>

namespace {
constexpr int kPenUpState = 1;   // SP state: 1 = up
constexpr int kPenDownState = 0; // SP state: 0 = down
constexpr int kSC_PenUpParam = 4;   // SC parameter for pen up
constexpr int kSC_PenDownParam = 5; // SC parameter for pen down
}

bool AxiDrawController::initialize(std::string *errorOut) {
    if (!setPenUpValue(m_state.penUpPos, errorOut)) return false;
    if (!setPenDownValue(m_state.penDownPos, errorOut)) return false;
    return true;
}

bool AxiDrawController::setPenUpValue(int v, std::string *errorOut) {
    m_state.penUpPos = v;
    recomputeUpDownMs();
    return sendCmd("SC," + std::to_string(kSC_PenUpParam) + "," + std::to_string(v), errorOut);
}

bool AxiDrawController::setPenDownValue(int v, std::string *errorOut) {
    m_state.penDownPos = v;
    recomputeUpDownMs();
    return sendCmd("SC," + std::to_string(kSC_PenDownParam) + "," + std::to_string(v), errorOut);
}

bool AxiDrawController::penUp(int durationMs, std::string *errorOut) {
    int dur = (durationMs >= 0) ? durationMs : m_state.upDownMs;
    return sendCmd("SP," + std::to_string(kPenUpState) + "," + std::to_string(dur), errorOut);
}

bool AxiDrawController::penDown(int durationMs, std::string *errorOut) {
    int dur = (durationMs >= 0) ? durationMs : m_state.upDownMs;
    return sendCmd("SP," + std::to_string(kPenDownState) + "," + std::to_string(dur), errorOut);
}

bool AxiDrawController::stepperMove(int durationMs, int aSteps, int bSteps, std::string *errorOut) {
    return sendCmd(
        "SM," + std::to_string(durationMs) + "," + std::to_string(aSteps) + "," + std::to_string(bSteps),
        errorOut);
}

bool AxiDrawController::lowLevelMove(int rateStepsPerSecond, int aSteps, int bSteps, std::string *errorOut) {
    return sendCmd(
        "LM," + std::to_string(rateStepsPerSecond) + "," + std::to_string(aSteps) + "," + std::to_string(bSteps),
        errorOut);
}

bool AxiDrawController::enableMotors(bool enable1, bool enable2, std::string *errorOut) {
    return sendCmd(
        std::string("EM,") + (enable1 ? "1" : "0") + "," + (enable2 ? "1" : "0"),
        errorOut);
}

bool AxiDrawController::disengageMotors(std::string *errorOut) {
    return enableMotors(false, false, errorOut);
}

bool AxiDrawController::reset(std::string *errorOut) {
    return sendCmd("R", errorOut);
}

bool AxiDrawController::sendCmd(const std::string &cmd, std::string *errorOut) {
    // Check connection and attempt reconnection if disconnected
    if (!m_serial.isConnected()) {
        LOG(WARNING) << "Serial not connected, attempting reconnection before sending: " << cmd;
        std::string reconnectErr;
        if (!m_serial.reconnect(&reconnectErr)) {
            if (errorOut) *errorOut = "Serial not connected and reconnection failed: " + reconnectErr;
            LOG(WARNING) << "AxiDraw send failed (not connected, reconnect failed): " << cmd;
            return false;
        }
        LOG(INFO) << "Reconnected successfully before sending command";
    }

    std::string err;
    bool ok = m_serial.writeLine(cmd, &err);
    if (!ok) {
        if (errorOut) *errorOut = err;
        LOG(ERROR) << "AxiDraw writeLine failed: " << err;
        return false;
    }

    // Read back the EBB response to confirm command was received.
    // The EBB responds with "OK" for most commands, or data lines followed by "OK".
    // We read lines until we get "OK" or hit a timeout.
    std::string response;
    m_lastResponse.clear();
    const int kMaxResponseLines = 10;
    for (int i = 0; i < kMaxResponseLines; ++i) {
        std::string line;
        if (!m_serial.readLine(line, 5000, &err)) {
            LOG(WARNING) << "No response from EBB for command '" << cmd << "': " << err;
            if (errorOut) *errorOut = "No response from EBB: " + err;
            return false;
        }
        if (line == "OK") {
            return true;
        }
        // Accumulate non-OK response lines (e.g., query results)
        if (!m_lastResponse.empty()) m_lastResponse += "\n";
        m_lastResponse += line;
    }

    LOG(WARNING) << "EBB did not respond with OK for command '" << cmd << "', got: " << m_lastResponse;
    if (errorOut) *errorOut = "Unexpected EBB response: " + m_lastResponse;
    return false;
}

bool AxiDrawController::queryStepPosition(int &aStepsOut, int &bStepsOut, std::string *errorOut) {
    if (!sendCmd("QS", errorOut)) return false;
    // QS response format: "a_steps,b_steps" (before the OK line, captured in m_lastResponse)
    int a = 0, b = 0;
    if (sscanf(m_lastResponse.c_str(), "%d,%d", &a, &b) != 2) {
        if (errorOut) *errorOut = "Failed to parse QS response: " + m_lastResponse;
        return false;
    }
    aStepsOut = a;
    bStepsOut = b;
    return true;
}

void AxiDrawController::recomputeUpDownMs() {
    int diff = std::abs(m_state.penUpPos - m_state.penDownPos);
    // upDownMs = |up-down| * 0.06
    m_state.upDownMs = static_cast<int>(std::lround(diff * 0.06));
    if (m_state.upDownMs < 1) m_state.upDownMs = 1;
}


