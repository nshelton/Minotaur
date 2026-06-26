#pragma once

#include <string>
#include <string_view>
#include <vector>

// Cross-platform serial controller for COM ports (Windows) and /dev/cu.* (macOS/Linux).
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
        std::string portPath{};              // Current port path (empty if not connected)
        std::string lastPortPath{};          // Last connected port (preserved after disconnect for reconnection)
        int baudRate{115200};
        int lastBaudRate{115200};            // Last used baud rate (preserved after disconnect)
        std::string lastError{};
    };

    SerialController() = default;
    ~SerialController();

    // Connect to a serial port (e.g., "COM3" on Windows, "/dev/cu.usbmodem11101" on macOS).
    // Returns true on success.
    bool connect(const std::string &portPath, int baud = 115200, std::string *errorOut = nullptr);

    // Disconnect if connected (preserves last port info for reconnection).
    void disconnect();

    // Reconnect to the last known port. Returns true on success.
    // Requires a previous connection or failed connection attempt.
    bool reconnect(std::string *errorOut = nullptr);

    // Returns true if the port is open.
    bool isConnected() const { return m_state.isConnected; }

    // Write an ASCII line, automatically appending a carriage return ("\r").
    bool writeLine(std::string_view asciiNoCR, std::string *errorOut = nullptr);

    // Read a line from the serial port (up to '\r' or '\n'), with timeout in ms.
    // Returns true if a line was read, false on timeout or error.
    bool readLine(std::string &lineOut, int timeoutMs = 2000, std::string *errorOut = nullptr);

    // Drain all pending data from the receive buffer (discard unread responses).
    void drainReceiveBuffer();

    const SerialState &state() const { return m_state; }

    // Enumerate COM ports with vendor/product IDs (Windows). Returns empty on failure.
    std::vector<PortInfo> listPorts(std::string *errorOut = nullptr) const;

    // Auto-connect to the first matching EBB (VID/PID defaults), returns true on success.
    bool autoConnect(std::string *chosenPortOut = nullptr, int baud = 115200, std::string *errorOut = nullptr);

    // Auto-connect by vendor/product IDs (uppercase hex strings), returns true on success.
    bool autoConnectByVidPid(const std::string &vendorIdUpper, const std::string &productIdUpper,
                             std::string *chosenPortOut = nullptr, int baud = 115200, std::string *errorOut = nullptr);

private:
    std::string normalizePortPath(const std::string &portPath) const;

    SerialState m_state{};

#ifdef _WIN32
    void *m_handle{reinterpret_cast<void *>(-1)}; // HANDLE without including windows.h in header
#else
    int m_fd{-1}; // POSIX file descriptor
#endif
};


