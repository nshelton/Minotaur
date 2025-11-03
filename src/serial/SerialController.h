#pragma once

#include <string>
#include <string_view>
#include <vector>

// Windows-only minimal serial controller for COM ports.
// Provides connect/disconnect and simple line-based write (appends CR).
class SerialController {
public:
    static constexpr const char* kEBB_VID = "04D8"; // Microchip VID used by EiBotBoard
    static constexpr const char* kEBB_PID = "FD92"; // EiBotBoard PID

    struct PortInfo {
        std::string path;          // e.g., "COM3"
        std::string vendorId;      // e.g., "04D8" (uppercase hex, no 0x)
        std::string productId;     // e.g., "FD92"
        std::string friendlyName;  // e.g., device label
    };

    struct SerialState {
        bool isConnected{false};
        std::string portPath{};
        int baudRate{115200};
        std::string lastError{};
    };

    SerialController() = default;
    ~SerialController();

    // Connect to a COM port (e.g., "COM3"). Returns true on success.
    bool connect(const std::string &portPath, int baud = 115200, std::string *errorOut = nullptr);

    // Disconnect if connected.
    void disconnect();

    // Returns true if the port is open.
    bool isConnected() const { return m_state.isConnected; }

    // Write an ASCII line, automatically appending a carriage return ("\r").
    bool writeLine(std::string_view asciiNoCR, std::string *errorOut = nullptr);

    const SerialState &state() const { return m_state; }

    // Enumerate COM ports with vendor/product IDs (Windows). Returns empty on failure.
    std::vector<PortInfo> listPorts(std::string *errorOut = nullptr) const;

    // Auto-connect to the first matching EBB (VID/PID defaults), returns true on success.
    bool autoConnect(std::string *chosenPortOut = nullptr, int baud = 115200, std::string *errorOut = nullptr);

    // Auto-connect by vendor/product IDs (uppercase hex strings), returns true on success.
    bool autoConnectByVidPid(const std::string &vendorIdUpper, const std::string &productIdUpper,
                             std::string *chosenPortOut = nullptr, int baud = 115200, std::string *errorOut = nullptr);

private:
    std::string normalizeWindowsComPath(const std::string &portPath) const;

    SerialState m_state{};

#ifdef _WIN32
    void *m_handle{reinterpret_cast<void *>(-1)}; // HANDLE without including windows.h in header
#endif
};


