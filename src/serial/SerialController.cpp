#include "serial/SerialController.h"

#include <glog/logging.h>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <setupapi.h>
#  include <devguid.h>
#  include <initguid.h>
#  pragma comment(lib, "setupapi.lib")

// Define error codes not available in all SDK versions
#ifndef ERROR_DEVICE_DOES_NOT_EXIST
#  define ERROR_DEVICE_DOES_NOT_EXIST 433L
#endif

#else
// POSIX (macOS / Linux)
#  include <fcntl.h>
#  include <unistd.h>
#  include <termios.h>
#  include <sys/ioctl.h>
#  include <cerrno>
#  include <cstring>
#  include <dirent.h>
#  include <fstream>
#  include <sstream>
#  include <algorithm>
#  ifdef __APPLE__
#    include <IOKit/IOKitLib.h>
#    include <IOKit/usb/IOUSBLib.h>
#    include <IOKit/serial/IOSerialKeys.h>
#    include <CoreFoundation/CoreFoundation.h>
#  endif
#endif

namespace {
#ifdef _WIN32
bool setCommParams(HANDLE h, int baud, std::string *err) {
    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(h, &dcb)) {
        if (err) {
            DWORD code = GetLastError();
            char *msg = nullptr;
            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, nullptr);
            *err = std::string("GetCommState failed (code ") + std::to_string(code) + ")" + (msg ? std::string(": ") + msg : std::string());
            if (msg) LocalFree(msg);
        }
        return false;
    }
    dcb.BaudRate = static_cast<DWORD>(baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    if (!SetCommState(h, &dcb)) {
        if (err) {
            DWORD code = GetLastError();
            char *msg = nullptr;
            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, nullptr);
            *err = std::string("SetCommState failed (code ") + std::to_string(code) + ")" + (msg ? std::string(": ") + msg : std::string());
            if (msg) LocalFree(msg);
        }
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 50;           // ms
    timeouts.ReadTotalTimeoutConstant = 200;     // ms
    timeouts.ReadTotalTimeoutMultiplier = 10;    // per byte
    timeouts.WriteTotalTimeoutConstant = 1000;   // ms (raise to tolerate device latency)
    timeouts.WriteTotalTimeoutMultiplier = 5;    // per byte
    if (!SetCommTimeouts(h, &timeouts)) {
        if (err) {
            DWORD code = GetLastError();
            char *msg = nullptr;
            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, nullptr);
            *err = std::string("SetCommTimeouts failed (code ") + std::to_string(code) + ")" + (msg ? std::string(": ") + msg : std::string());
            if (msg) LocalFree(msg);
        }
        return false;
    }

    // Configure driver queue sizes (best-effort)
    SetupComm(h, 4096, 4096);

    // Clear any stale data
    PurgeComm(h, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
    return true;
}
#else
// Map integer baud rate to POSIX speed_t constant
static speed_t baudToSpeed(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B115200;
    }
}

static bool configurePosixPort(int fd, int baud, std::string *err) {
    struct termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        if (err) *err = std::string("tcgetattr failed: ") + strerror(errno);
        return false;
    }

    speed_t speed = baudToSpeed(baud);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // 8N1, no flow control (matches the Windows DCB settings)
    tty.c_cflag &= ~PARENB;        // No parity
    tty.c_cflag &= ~CSTOPB;        // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;            // 8 data bits
    tty.c_cflag &= ~CRTSCTS;       // No hardware flow control
    tty.c_cflag |= CREAD | CLOCAL; // Enable receiver, ignore modem control

    // Raw input
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);           // No software flow control
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // Raw output
    tty.c_oflag &= ~OPOST;

    // Timeouts: VMIN=0, VTIME=2 → non-blocking with 200ms timeout
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 2;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        if (err) *err = std::string("tcsetattr failed: ") + strerror(errno);
        return false;
    }

    // Flush any stale data
    tcflush(fd, TCIOFLUSH);
    return true;
}

#ifdef __APPLE__
// Helper: extract a CFString property from an IOKit service as std::string
static std::string getCFStringProperty(io_object_t service, CFStringRef key) {
    CFTypeRef ref = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (!ref) return {};
    std::string result;
    if (CFGetTypeID(ref) == CFStringGetTypeID()) {
        char buf[256]{};
        if (CFStringGetCString(static_cast<CFStringRef>(ref), buf, sizeof(buf), kCFStringEncodingUTF8)) {
            result = buf;
        }
    }
    CFRelease(ref);
    return result;
}

// Helper: extract a CFNumber property as int
static int getCFNumberProperty(io_object_t service, CFStringRef key) {
    CFTypeRef ref = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (!ref) return -1;
    int val = -1;
    if (CFGetTypeID(ref) == CFNumberGetTypeID()) {
        CFNumberGetValue(static_cast<CFNumberRef>(ref), kCFNumberIntType, &val);
    }
    CFRelease(ref);
    return val;
}

// Walk up the IOKit registry from a serial service to find the USB device ancestor
// and extract VID/PID
static bool getUSBVidPid(io_object_t serialService, std::string &vid, std::string &pid) {
    io_object_t parent = 0;
    io_object_t current = serialService;
    IOObjectRetain(current);

    while (IORegistryEntryGetParentEntry(current, kIOServicePlane, &parent) == KERN_SUCCESS) {
        IOObjectRelease(current);
        current = parent;

        int vendorId = getCFNumberProperty(current, CFSTR("idVendor"));
        int productId = getCFNumberProperty(current, CFSTR("idProduct"));
        if (vendorId >= 0 && productId >= 0) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%04X", vendorId);
            vid = buf;
            snprintf(buf, sizeof(buf), "%04X", productId);
            pid = buf;
            IOObjectRelease(current);
            return true;
        }
    }
    IOObjectRelease(current);
    return false;
}
#endif // __APPLE__
#endif // _WIN32
} // namespace

SerialController::~SerialController() {
    disconnect();
}

bool SerialController::connect(const std::string &portPath, int baud, std::string *errorOut) {
    disconnect();
    m_state.lastError.clear();

    // Always preserve connection attempt info for reconnection
    m_state.lastPortPath = portPath;
    m_state.lastBaudRate = baud;

#ifdef _WIN32
    std::string normalized = normalizePortPath(portPath);
    HANDLE h = CreateFileA(
        normalized.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (h == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        char *msg = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, nullptr);
        m_state.isConnected = false;
        m_state.portPath.clear();
        m_state.baudRate = baud;
        m_state.lastError = std::string("Failed to open port (code ") + std::to_string(code) + ")" + (msg ? std::string(": ") + msg : std::string());
        if (errorOut) *errorOut = m_state.lastError;
        if (msg) LocalFree(msg);
        return false;
    }

    std::string err;
    if (!setCommParams(h, baud, &err)) {
        CloseHandle(h);
        m_state.isConnected = false;
        m_state.portPath.clear();
        m_state.baudRate = baud;
        m_state.lastError = err;
        if (errorOut) *errorOut = m_state.lastError;
        return false;
    }

    m_handle = h;
    m_state.isConnected = true;
    m_state.portPath = portPath;
    m_state.baudRate = baud;
    m_state.lastError.clear();
    LOG(INFO) << "Serial connected: " << portPath << " @" << baud;
    return true;
#else
    int fd = open(portPath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        m_state.isConnected = false;
        m_state.portPath.clear();
        m_state.baudRate = baud;
        m_state.lastError = std::string("Failed to open port: ") + strerror(errno);
        if (errorOut) *errorOut = m_state.lastError;
        return false;
    }

    // Clear O_NONBLOCK after open (we opened non-blocking to avoid hanging on DCD)
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }

    // Acquire exclusive lock (like Windows share-mode 0)
    if (ioctl(fd, TIOCEXCL) != 0) {
        LOG(WARNING) << "Could not set exclusive mode on " << portPath << ": " << strerror(errno);
    }

    std::string err;
    if (!configurePosixPort(fd, baud, &err)) {
        close(fd);
        m_state.isConnected = false;
        m_state.portPath.clear();
        m_state.baudRate = baud;
        m_state.lastError = err;
        if (errorOut) *errorOut = m_state.lastError;
        return false;
    }

    m_fd = fd;
    m_state.isConnected = true;
    m_state.portPath = portPath;
    m_state.baudRate = baud;
    m_state.lastError.clear();
    LOG(INFO) << "Serial connected: " << portPath << " @" << baud;
    return true;
#endif
}

void SerialController::disconnect() {
#ifdef _WIN32
    if (m_state.isConnected && m_handle && m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(reinterpret_cast<HANDLE>(m_handle));
    }
#else
    if (m_state.isConnected && m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
#endif
    m_state.isConnected = false;
    m_state.portPath.clear();
    // Note: lastPortPath and lastBaudRate are intentionally preserved for reconnection
}

bool SerialController::reconnect(std::string *errorOut) {
    if (m_state.lastPortPath.empty()) {
        m_state.lastError = "No previous connection to reconnect to";
        if (errorOut) *errorOut = m_state.lastError;
        return false;
    }

    LOG(INFO) << "Attempting to reconnect to " << m_state.lastPortPath << " @" << m_state.lastBaudRate;
    return connect(m_state.lastPortPath, m_state.lastBaudRate, errorOut);
}

bool SerialController::writeLine(std::string_view asciiNoCR, std::string *errorOut) {
    if (!m_state.isConnected) {
        if (errorOut) *errorOut = "Port not connected";
        m_state.lastError = "Port not connected";
        return false;
    }

#ifdef _WIN32
    std::string withCR;
    withCR.reserve(asciiNoCR.size() + 1);
    withCR.append(asciiNoCR.begin(), asciiNoCR.end());
    withCR.push_back('\r');

    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(reinterpret_cast<HANDLE>(m_handle), withCR.data(), static_cast<DWORD>(withCR.size()), &bytesWritten, nullptr);
    if (!ok || bytesWritten != withCR.size()) {
        DWORD code = GetLastError();
        char *msg = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, nullptr);
        std::string firstErr = std::string("WriteFile failed (code ") + std::to_string(code) + ")" + (msg ? std::string(": ") + msg : std::string());
        if (msg) LocalFree(msg);

        // Best-effort recovery: clear errors/queues and retry once
        DWORD commErrors = 0;
        COMSTAT commStat{};
        ClearCommError(reinterpret_cast<HANDLE>(m_handle), &commErrors, &commStat);
        PurgeComm(reinterpret_cast<HANDLE>(m_handle), PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
        Sleep(50);

        bytesWritten = 0;
        ok = WriteFile(reinterpret_cast<HANDLE>(m_handle), withCR.data(), static_cast<DWORD>(withCR.size()), &bytesWritten, nullptr);
        if (!ok || bytesWritten != withCR.size()) {
            code = GetLastError();
            msg = nullptr;
            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, nullptr);

            // If device disappeared (USB suspend/disconnect), try reconnecting
            if (code == ERROR_DEVICE_DOES_NOT_EXIST || code == ERROR_GEN_FAILURE || code == ERROR_NOT_READY) {
                LOG(WARNING) << "Device appears disconnected (code " << code << "), attempting reconnection...";
                disconnect();
                Sleep(500); // Give Windows time to re-enumerate

                std::string reconnectErr;
                if (reconnect(&reconnectErr)) {
                    LOG(INFO) << "Reconnected successfully, retrying write...";
                    Sleep(100);
                    bytesWritten = 0;
                    ok = WriteFile(reinterpret_cast<HANDLE>(m_handle), withCR.data(), static_cast<DWORD>(withCR.size()), &bytesWritten, nullptr);
                    if (ok && bytesWritten == withCR.size()) {
                        LOG(INFO) << "Write succeeded after reconnection";
                        return true;
                    } else {
                        LOG(WARNING) << "Write failed even after successful reconnection";
                    }
                } else {
                    LOG(WARNING) << "Reconnection failed: " << reconnectErr;
                }
            }

            m_state.lastError = firstErr + std::string("; retry failed (code ") + std::to_string(code) + ")" + (msg ? std::string(": ") + msg : std::string());
            if (errorOut) *errorOut = m_state.lastError + std::string("; wrote ") + std::to_string(bytesWritten) + "/" + std::to_string(withCR.size()) + " bytes";
            if (msg) LocalFree(msg);
            return false;
        }
    }
    return true;
#else
    std::string withCR;
    withCR.reserve(asciiNoCR.size() + 1);
    withCR.append(asciiNoCR.begin(), asciiNoCR.end());
    withCR.push_back('\r');

    ssize_t written = ::write(m_fd, withCR.data(), withCR.size());
    if (written < 0) {
        int e = errno;
        std::string firstErr = std::string("write failed: ") + strerror(e);

        // If device disappeared, try reconnecting
        if (e == EIO || e == ENXIO || e == ENODEV) {
            LOG(WARNING) << "Device appears disconnected (errno " << e << "), attempting reconnection...";
            disconnect();
            usleep(500000); // 500ms

            std::string reconnectErr;
            if (reconnect(&reconnectErr)) {
                LOG(INFO) << "Reconnected successfully, retrying write...";
                usleep(100000); // 100ms
                written = ::write(m_fd, withCR.data(), withCR.size());
                if (written == static_cast<ssize_t>(withCR.size())) {
                    LOG(INFO) << "Write succeeded after reconnection";
                    return true;
                } else {
                    LOG(WARNING) << "Write failed even after successful reconnection";
                }
            } else {
                LOG(WARNING) << "Reconnection failed: " << reconnectErr;
            }
        }

        m_state.lastError = firstErr;
        if (errorOut) *errorOut = m_state.lastError;
        return false;
    }
    if (static_cast<size_t>(written) != withCR.size()) {
        m_state.lastError = "Partial write: " + std::to_string(written) + "/" + std::to_string(withCR.size()) + " bytes";
        if (errorOut) *errorOut = m_state.lastError;
        return false;
    }
    return true;
#endif
}

std::string SerialController::normalizePortPath(const std::string &portPath) const {
#ifdef _WIN32
    if (portPath.rfind("\\\\.\\", 0) == 0) {
        return portPath; // already normalized
    }
    // For COM10+ Windows requires \\.\ prefix; it's also fine for COM1..9
    return std::string("\\\\.\\") + portPath;
#else
    return portPath;
#endif
}

std::vector<SerialController::PortInfo> SerialController::listPorts(std::string *errorOut) const {
    std::vector<PortInfo> result;
#ifdef _WIN32
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        if (errorOut) *errorOut = "SetupDiGetClassDevs failed";
        return result;
    }

    SP_DEVINFO_DATA devInfo{};
    devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo); ++i) {
        // Friendly name
        char friendly[256]{};
        DWORD regType = 0, size = sizeof(friendly);
        if (!SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfo, SPDRP_FRIENDLYNAME, &regType, reinterpret_cast<PBYTE>(friendly), size, nullptr)) {
            friendly[0] = '\0';
        }

        // Open device registry key to read PortName (e.g., COM3)
        HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hKey == INVALID_HANDLE_VALUE) {
            continue;
        }

        char portName[64]{};
        DWORD portNameLen = sizeof(portName);
        LONG r = RegQueryValueExA(hKey, "PortName", nullptr, nullptr, reinterpret_cast<LPBYTE>(portName), &portNameLen);
        RegCloseKey(hKey);
        if (r != ERROR_SUCCESS) {
            continue;
        }

        // Hardware IDs (contains VID_xxxx&PID_yyyy for USB devices)
        char hwids[1024]{};
        if (!SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfo, SPDRP_HARDWAREID, &regType, reinterpret_cast<PBYTE>(hwids), sizeof(hwids), nullptr)) {
            hwids[0] = '\0';
        }

        std::string vid, pid;
        if (hwids[0] != '\0') {
            std::string ids = hwids;
            // normalize to upper
            for (char &c : ids) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
            auto vpos = ids.find("VID_");
            auto ppos = ids.find("PID_");
            if (vpos != std::string::npos && vpos + 8 <= ids.size()) {
                vid = ids.substr(vpos + 4, 4);
            }
            if (ppos != std::string::npos && ppos + 8 <= ids.size()) {
                pid = ids.substr(ppos + 4, 4);
            }
        }

        PortInfo info;
        info.path = portName;            // e.g., "COM3"
        info.vendorId = vid;             // e.g., "04D8"
        info.productId = pid;            // e.g., "FD92"
        info.friendlyName = friendly;    // best-effort
        result.push_back(std::move(info));
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return result;
#elif defined(__APPLE__)
    // Use IOKit to enumerate serial ports with USB VID/PID info
    CFMutableDictionaryRef matchDict = IOServiceMatching(kIOSerialBSDServiceValue);
    if (!matchDict) {
        if (errorOut) *errorOut = "IOServiceMatching failed";
        return result;
    }
    CFDictionarySetValue(matchDict, CFSTR(kIOSerialBSDTypeKey), CFSTR(kIOSerialBSDAllTypes));

    io_iterator_t iter = 0;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matchDict, &iter);
    if (kr != KERN_SUCCESS) {
        if (errorOut) *errorOut = "IOServiceGetMatchingServices failed";
        return result;
    }

    io_object_t service;
    while ((service = IOIteratorNext(iter)) != 0) {
        PortInfo info;
        info.path = getCFStringProperty(service, CFSTR(kIOCalloutDeviceKey));
        info.friendlyName = getCFStringProperty(service, CFSTR(kIOTTYDeviceKey));
        getUSBVidPid(service, info.vendorId, info.productId);
        if (!info.path.empty()) {
            result.push_back(std::move(info));
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iter);
    return result;
#else
    // Linux: enumerate /dev/ttyUSB* and /dev/ttyACM*
    // Basic enumeration without VID/PID (would need udev for that)
    DIR *dir = opendir("/dev");
    if (!dir) {
        if (errorOut) *errorOut = "Cannot open /dev";
        return result;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.find("ttyUSB") == 0 || name.find("ttyACM") == 0) {
            PortInfo info;
            info.path = "/dev/" + name;
            info.friendlyName = name;
            result.push_back(std::move(info));
        }
    }
    closedir(dir);
    return result;
#endif
}

bool SerialController::autoConnect(std::string *chosenPortOut, int baud, std::string *errorOut) {
    return autoConnectByVidPid(kEBB_VID, kEBB_PID, chosenPortOut, baud, errorOut);
}

bool SerialController::autoConnectByVidPid(const std::string &vendorIdUpper, const std::string &productIdUpper,
                                           std::string *chosenPortOut, int baud, std::string *errorOut) {
    std::string err;
    auto ports = listPorts(&err);
    if (!err.empty() && ports.empty()) {
        if (errorOut) *errorOut = err;
        return false;
    }
    for (const auto &p : ports) {
        if (!p.vendorId.empty() && !p.productId.empty() && p.vendorId == vendorIdUpper && p.productId == productIdUpper) {
            std::string cErr;
            if (connect(p.path, baud, &cErr)) {
                if (chosenPortOut) *chosenPortOut = p.path;
                return true;
            } else {
                if (errorOut) *errorOut = cErr;
                return false;
            }
        }
    }
    if (errorOut) *errorOut = "No matching device found";
    return false;
}
