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
#endif
} // namespace

SerialController::~SerialController() {
    disconnect();
}

bool SerialController::connect(const std::string &portPath, int baud, std::string *errorOut) {
    disconnect();
    m_state.lastError.clear();

#ifdef _WIN32
    std::string normalized = normalizeWindowsComPath(portPath);
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
    (void)portPath; (void)baud; (void)errorOut;
    m_state.isConnected = false;
    m_state.lastError = "Serial not supported on this platform";
    if (errorOut) *errorOut = m_state.lastError;
    return false;
#endif
}

void SerialController::disconnect() {
#ifdef _WIN32
    if (m_state.isConnected && m_handle && m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(reinterpret_cast<HANDLE>(m_handle));
    }
#endif
    m_state.isConnected = false;
    m_state.portPath.clear();
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
            m_state.lastError = firstErr + std::string("; retry failed (code ") + std::to_string(code) + ")" + (msg ? std::string(": ") + msg : std::string());
            if (errorOut) *errorOut = m_state.lastError + std::string("; wrote ") + std::to_string(bytesWritten) + "/" + std::to_string(withCR.size()) + " bytes";
            if (msg) LocalFree(msg);
            return false;
        }
    }
    return true;
#else
    (void)asciiNoCR; (void)errorOut;
    m_state.lastError = "Serial not supported on this platform";
    if (errorOut) *errorOut = m_state.lastError;
    return false;
#endif
}

std::string SerialController::normalizeWindowsComPath(const std::string &portPath) const {
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
#else
    if (errorOut) *errorOut = "Not supported";
    return result;
#endif
}

bool SerialController::autoConnect(std::string *chosenPortOut, int baud, std::string *errorOut) {
    return autoConnectByVidPid(kEBB_VID, kEBB_PID, chosenPortOut, baud, errorOut);
}

bool SerialController::autoConnectByVidPid(const std::string &vendorIdUpper, const std::string &productIdUpper,
                                           std::string *chosenPortOut, int baud, std::string *errorOut) {
#ifdef _WIN32
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
#else
    (void)vendorIdUpper; (void)productIdUpper; (void)chosenPortOut; (void)baud;
    if (errorOut) *errorOut = "Not supported";
    return false;
#endif
}


