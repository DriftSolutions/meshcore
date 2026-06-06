/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

#ifdef WIN32
// Windows driver
#include <setupapi.h>
#include <devguid.h>
#pragma comment(lib, "Setupapi.lib")

static map<string, string> EnumerateSerialPorts() {
    map<string, string> ports; // { port_name, friendly_name }

    HDEVINFO hDevs = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (hDevs == INVALID_HANDLE_VALUE) {
        return ports;
    }

    SP_DEVINFO_DATA did = { sizeof(did) };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevs, i, &did); i++) {
        // Get the actual port name (e.g. "COM3") from the device registry key
        HKEY hKey = SetupDiOpenDevRegKey(hDevs, &did, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hKey == INVALID_HANDLE_VALUE) {
            continue;
        }

        char portName[32] = { 0 };
        DWORD portNameSize = sizeof(portName);
        DWORD type = 0;
        bool gotPort = (RegQueryValueExA(hKey, "PortName", NULL, &type, (BYTE*)portName, &portNameSize) == ERROR_SUCCESS);
        RegCloseKey(hKey);

        if (!gotPort || _strnicmp(portName, "COM", 3) != 0) {
            continue; // skip LPT ports
        }

        // Get friendly name (e.g. "USB Serial Device (COM3)")
        DWORD bufSize = 0;
        SetupDiGetDeviceRegistryPropertyA(hDevs, &did, SPDRP_FRIENDLYNAME, &type, NULL, 0, &bufSize);
        string friendlyName;
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && bufSize > 0) {
            char* buf = (char*)malloc(bufSize + 1);
            if (buf) {
                memset(buf, 0, bufSize + 1);
                if (SetupDiGetDeviceRegistryPropertyA(hDevs, &did, SPDRP_FRIENDLYNAME, &type, (BYTE*)buf, bufSize, &bufSize)) {
                    friendlyName = buf;
                }
                free(buf);
            }
        }

        ports[portName] = friendlyName;
    }

    SetupDiDestroyDeviceInfoList(hDevs);
    return ports;
}

bool IO_Driver_Serial::Open(const string& devname) {
    Close();

    string device;
    if (!strnicmp(devname.c_str(), "COM", 3) && devname[devname.length() - 1] == ':') {
        device = devname;
        device.pop_back();
    } else {
        static map<string, string> ports;

        auto new_ports = EnumerateSerialPorts();
        if (new_ports.size()) {
            ports = new_ports;
        }

        if (ports.empty()) {
            printf("No serial ports found.\n");
            return false;
        }

        printf("Available serial ports:\n");
        for (auto& p : ports) {
            if (p.second.empty()) {
                printf("\t%s\n", p.first.c_str());
            } else {
                printf("\t%s: %s\n", p.first.c_str(), p.second.c_str());
            }
            if (!strnicmp(devname.c_str(), p.first.c_str(), devname.length()) || !strnicmp(devname.c_str(), p.second.c_str(), devname.length())) {
                device = p.first;
                printf("Found port '%s' for '%s'\n", device.c_str(), devname.c_str());
                break;
            }
        }

        if (device.empty()) {
            printf("Could not find port matching '%s'!\n", devname.c_str());
            return false;
        }
    }

    // Prepend \\.\\ for CreateFile — required for COM10 and above, harmless for lower ports
    if (strnicmp(device.c_str(), "COM", 3) == 0) {
        device = "\\\\.\\" + device;
    }

    printf("Opening %s...\n", device.c_str());
    hPort = CreateFile(device.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPort == INVALID_HANDLE_VALUE) {
        printf("Error opening COM port! (error: %u)\n", GetLastError());
        return false;
    }

    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(DCB);

    if (!GetCommState(hPort, &dcb)) {
        printf("Error getting COM port state! (error: %u)\n", GetLastError());
        return false;
    }

    dcb.BaudRate = CBR_115200;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fAbortOnError = FALSE;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(hPort, &dcb)) {
        printf("Error setting COM port state! (error: %u)\n", GetLastError());
        return false;
    }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 500;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(hPort, &timeouts)) {
        printf("Error setting COM port timeouts! (error: %u)\n", GetLastError());
        return false;
    }

    if (config.last_start_was_timeout) {
        Reboot();
        mqtt_error("Waiting 20 seconds for reboot...");
        safe_sleep(20);
    }

    return true;
}

bool IO_Driver_Serial::IsOpen() {
    return (hPort != INVALID_HANDLE_VALUE);
}

void IO_Driver_Serial::Close() {
    if (hPort != INVALID_HANDLE_VALUE) {
        printf("Closing COM port...\n");
        CloseHandle(hPort);
        hPort = INVALID_HANDLE_VALUE;
    }
}

int IO_Driver_Serial::Read(uint8* buf, int buflen) {
    if (hPort == INVALID_HANDLE_VALUE || buflen <= 0) {
        return -1;
    }

    DWORD bread = 0;
    if (!ReadFile(hPort, buf, (DWORD)buflen, &bread, NULL)) {
        return -1;
    }

    return (int)bread;
}

int IO_Driver_Serial::Write(const uint8* buf, int buflen) {
    if (hPort == INVALID_HANDLE_VALUE || buflen <= 0) {
        return -1;
    }

    DWORD bread = 0;
    if (!WriteFile(hPort, buf, (DWORD)buflen, &bread, NULL)) {
        return -1;
    }

    return (int)bread;
}

bool IO_Driver_Serial::Reboot() {
    if (hPort == INVALID_HANDLE_VALUE) {
        return false;
    }
    printf("Pulsing RTS to trigger hardware reset...\n");
    EscapeCommFunction(hPort, CLRDTR);  // DTR low = GPIO0 high = normal boot mode
    EscapeCommFunction(hPort, SETRTS);  // RTS high -> transistor pulls EN low -> reset
    Sleep(100);
    EscapeCommFunction(hPort, CLRRTS);  // RTS low -> EN returns high -> device boots
    return true;
}

#else
// Linux driver
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

bool IO_Driver_Serial::Open(const string& device) {
    Close();

    printf("Opening %s...\n", device.c_str());
    fd = open(device.c_str(), O_RDWR | O_NOCTTY);
    if (fd == -1) {
        printf("Error opening serial port! (error: %s)\n", strerror(errno));
        return false;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        printf("Error getting serial port state! (error: %s)\n", strerror(errno));
        Close();
        return false;
    }

    // Raw mode — disables all special processing
    cfmakeraw(&tty);

    // 115200 baud
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    // 8N1
    tty.c_cflag &= ~PARENB;   // no parity
    tty.c_cflag &= ~CSTOPB;   // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    // No hardware flow control
    tty.c_cflag &= ~CRTSCTS;

    // Enable receiver, ignore modem control lines
    tty.c_cflag |= CREAD | CLOCAL;

    // No software flow control
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    // VMIN=0, VTIME=5: return whatever is available after 500ms
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 5;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error setting serial port state! (error: %s)\n", strerror(errno));
        Close();
        return false;
    }

    if (config.last_start_was_timeout) {
        Reboot();
        mqtt_error("Waiting 20 seconds for reboot...");
        safe_sleep(20);
    }

    return true;
}

bool IO_Driver_Serial::IsOpen() {
    return (fd != -1);
}

void IO_Driver_Serial::Close() {
    if (fd != -1) {
        printf("Closing serial port...\n");
        close(fd);
        fd = -1;
    }
}

int IO_Driver_Serial::Read(uint8* buf, int buflen) {
    if (fd == -1 || buflen <= 0) {
        return -1;
    }

    return read(fd, buf, buflen);
}

int IO_Driver_Serial::Write(const uint8* buf, int buflen) {
    if (fd == -1 || buflen <= 0) {
        return -1;
    }

    return write(fd, buf, buflen);
}

bool IO_Driver_Serial::Reboot() {
    if (fd == -1) {
        return false;
    }

    printf("Pulsing DTR to trigger hardware reset...\n");
    int flags = TIOCM_DTR;
    ioctl(fd, TIOCMBIC, &flags); // lower DTR
    usleep(200000);              // hold low for 200ms
    ioctl(fd, TIOCMBIS, &flags); // raise DTR

    return true;
}

#endif
