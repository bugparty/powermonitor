#include <asio.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <iomanip>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#pragma comment(lib, "setupapi.lib")
#else
#include <dirent.h>
#include <cstring>
#include <fstream>
#endif

struct PortInfo {
    std::string port;
    std::string description;
    std::string hardware_id;  // VID:PID
    std::string manufacturer;
};

#ifdef _WIN32
static std::string get_registry_property(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, DWORD property) {
    char buffer[512] = {0};
    if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, devInfoData, property,
                                           nullptr, (PBYTE)buffer, sizeof(buffer), nullptr)) {
        return std::string(buffer);
    }
    return "";
}
#else
static std::string read_sysfs(const std::string& path) {
    std::ifstream file(path);
    if (file) {
        std::string content;
        std::getline(file, content);
        return content;
    }
    return "";
}
#endif

std::vector<PortInfo> list_ports() {
    std::vector<PortInfo> ports;

#ifdef _WIN32
    // Windows: use SetupAPI to enumerate serial ports
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return ports;
    }

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
        PortInfo info;

        // Get friendly name
        std::string friendlyName = get_registry_property(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
        if (friendlyName.empty()) continue;

        // Extract COM port number from friendly name
        size_t start = friendlyName.find("(COM");
        size_t end = friendlyName.find(")", start);
        if (start == std::string::npos || end == std::string::npos) continue;

        info.port = friendlyName.substr(start + 1, end - start - 1);
        info.description = get_registry_property(hDevInfo, &devInfoData, SPDRP_DEVICEDESC);
        info.manufacturer = get_registry_property(hDevInfo, &devInfoData, SPDRP_MFG);

        // Get Hardware ID (contains VID/PID)
        std::string hwid = get_registry_property(hDevInfo, &devInfoData, SPDRP_HARDWAREID);
        // Parse VID and PID
        size_t vidPos = hwid.find("VID_");
        size_t pidPos = hwid.find("PID_");
        if (vidPos != std::string::npos && pidPos != std::string::npos) {
            std::string vid = hwid.substr(vidPos + 4, 4);
            std::string pid = hwid.substr(pidPos + 4, 4);
            info.hardware_id = vid + ":" + pid;
        } else {
            info.hardware_id = hwid;
        }

        ports.push_back(info);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    // Sort by COM port number
    std::sort(ports.begin(), ports.end(), [](const PortInfo& a, const PortInfo& b) {
        auto getNum = [](const std::string& s) -> int {
            size_t pos = s.find("COM");
            if (pos != std::string::npos) {
                try {
                    return std::stoi(s.substr(pos + 3));
                } catch (...) {
                    return 0;
                }
            }
            return 0;
        };
        return getNum(a.port) < getNum(b.port);
    });

#else
    // Linux: scan /dev directory and read sysfs info
    DIR* dir = opendir("/dev");
    if (!dir) {
        return ports;
    }


    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find("ttyUSB") == 0 || name.find("ttyACM") == 0) {
            PortInfo info;
            info.port = "/dev/" + name;

            // Read sysfs info
            std::string sysfs_base = "/sys/class/tty/" + name + "/device";
            std::string usb_base;

            // Resolve USB device path
            if (name.find("ttyUSB") == 0) {
                usb_base = sysfs_base + "/../..";
            } else if (name.find("ttyACM") == 0) {
                usb_base = sysfs_base + "/..";
            }

            info.manufacturer = read_sysfs(usb_base + "/manufacturer");
            info.description = read_sysfs(usb_base + "/product");

            std::string vid = read_sysfs(usb_base + "/idVendor");
            std::string pid = read_sysfs(usb_base + "/idProduct");
            if (!vid.empty() && !pid.empty()) {
                info.hardware_id = vid + ":" + pid;
            }

            ports.push_back(info);
        }
    }
    closedir(dir);

    std::sort(ports.begin(), ports.end(), [](const PortInfo& a, const PortInfo& b) {
        return a.port < b.port;
    });
#endif

    return ports;
}

class SerialPort {
public:
    SerialPort(asio::io_context& io, const std::string& port, unsigned int baud_rate)
        : io_(io), serial_(io) {

        serial_.open(port);

        serial_.set_option(asio::serial_port_base::baud_rate(baud_rate));
        serial_.set_option(asio::serial_port_base::character_size(8));
        serial_.set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::none));
        serial_.set_option(asio::serial_port_base::stop_bits(asio::serial_port_base::stop_bits::one));
        serial_.set_option(asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::none));

#ifdef _WIN32
        HANDLE hPort = serial_.native_handle();

        DCB comDCB = {};
        comDCB.DCBlength = sizeof(DCB);

        BOOL ok = GetCommState(hPort, &comDCB);
        if (!ok) {
            DWORD err = GetLastError();
            std::cerr << "GetCommState failed with error: " << err << std::endl;
            // Handle error
        }
        //comDCB.fRtsControl = RTS_CONTROL_DISABLE;
        //comDCB.fDtrControl = DTR_CONTROL_DISABLE;
        ok = SetCommState(hPort, &comDCB);
        if (!ok) {
            DWORD err = GetLastError();
            std::cerr << "SetCommState failed with error: " << err << std::endl;
        }

        // DCB dcb{};
        // dcb.DCBlength = sizeof(DCB);
        // if (GetCommState(handle, &dcb)) {
        //     dcb.fRtsControl = RTS_CONTROL_ENABLE;
        //     dcb.fOutxCtsFlow = FALSE;
        //     dcb.fDtrControl = DTR_CONTROL_ENABLE;
        //     dcb.fOutxDsrFlow = FALSE;
        //     if (!SetCommState(handle, &dcb)) {
        //         std::cerr << "Failed to apply DCB flow control flags" << std::endl;
        //     }
        // } else {
        //     std::cerr << "Failed to read DCB state" << std::endl;
        // }
        //EscapeCommFunction(hPort, SETRTS);
        //EscapeCommFunction(hPort, SETDTR);
#else
        int handle = serial_.native_handle();
        int status;
        if (ioctl(handle, TIOCMGET, &status) == 0) {
            status |= TIOCM_RTS;
            status |= TIOCM_DTR;
            ioctl(handle, TIOCMSET, &status);
        }
#endif

        std::cout << "Serial port opened: " << port << " @ " << baud_rate << " baud" << std::endl;
    }

    ~SerialPort() {
        stop();
    }

    void start_read() {
        do_read();
    }

    void stop() {
        std::cout << "\nClosing serial port..." << std::endl;
        if (serial_.is_open()) {
            asio::error_code ec;
            serial_.cancel(ec);
            serial_.close(ec);
        }
    }

    // Synchronous write
    size_t write(const std::vector<uint8_t>& data) {
        asio::error_code ec;
        size_t bytes = asio::write(serial_, asio::buffer(data), ec);
        if (ec) {
            std::cerr << "Write error: " << ec.message() << std::endl;
            return 0;
        }
        return bytes;
    }

    // Asynchronous write
    void async_write(const std::vector<uint8_t>& data) {
        auto buf = std::make_shared<std::vector<uint8_t>>(data);
        asio::async_write(serial_, asio::buffer(*buf),
            [buf](const asio::error_code& ec, size_t bytes) {
                if (ec) {
                    std::cerr << "Async write error: " << ec.message() << std::endl;
                } else {
                    std::cout << "Wrote " << bytes << " bytes" << std::endl;
                }
            });
    }

    uint64_t bytes_received() const { return bytes_received_; }

private:
    void do_read() {
        serial_.async_read_some(asio::buffer(read_buf_),
            [this](const asio::error_code& ec, size_t bytes) {
                if (!ec) {
                    bytes_received_ += bytes;
                    on_data_received(read_buf_.data(), bytes);
                    do_read();  // Continue reading
                } else if (ec != asio::error::operation_aborted) {
                    std::cerr << "Read error: " << ec.message() << std::endl;
                }
            });
    }

    void on_data_received(const uint8_t* data, size_t len) {
        std::cout << "Received " << len << " bytes: ";
        for (size_t i = 0; i < std::min(len, size_t(16)); ++i) {
            std::cout << std::hex << std::setw(3) << std::setfill('0')
                      << static_cast<int>(data[i]) << " ";
        }
        if (len > 16) std::cout << "...";
        std::cout << std::dec << std::endl;

    }

    asio::io_context& io_;
    asio::serial_port serial_;
    std::array<uint8_t, 4096> read_buf_;
    uint64_t bytes_received_ = 0;
};

void print_ports() {
    auto ports = list_ports();
    if (ports.empty()) {
        std::cout << "No serial ports found." << std::endl;
    } else {
        std::cout << "Available serial ports:\n" << std::endl;
        for (const auto& p : ports) {
            std::cout << "  " << std::left << std::setw(10) << p.port;
            if (!p.hardware_id.empty()) {
                std::cout << " [" << p.hardware_id << "]";
            }
            std::cout << std::endl;
            if (!p.description.empty()) {
                std::cout << "            Description:  " << p.description << std::endl;
            }
            if (!p.manufacturer.empty()) {
                std::cout << "            Manufacturer: " << p.manufacturer << std::endl;
            }
            std::cout << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    // ls command - list available serial ports
    if (argc >= 2 && std::string(argv[1]) == "ls") {
        print_ports();
        return 0;
    }

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <port> [baud_rate]" << std::endl;
        std::cout << "       " << argv[0] << " ls                  - List available ports" << std::endl;
        std::cout << std::endl;
        std::cout << "Examples:" << std::endl;
        std::cout << "  " << argv[0] << " ls" << std::endl;
        std::cout << "  " << argv[0] << " COM3 115200" << std::endl;
        std::cout << "  " << argv[0] << " /dev/ttyUSB0 115200" << std::endl;
        return 1;
    }

    std::string port = argv[1];
    unsigned int baud_rate = argc > 2 ? std::stoi(argv[2]) : 115200;

    try {
        asio::io_context io;

        SerialPort serial(io, port, baud_rate);
        serial.start_read();

        // Run io_context in a separate thread
        std::atomic<bool> running{true};
        std::thread io_thread([&io, &running] {
            while (running) {
                io.run_for(std::chrono::milliseconds(100));
                io.restart();
            }
        });

        std::cout << "\nCommands:" << std::endl;
        std::cout << "  ls                - List available serial ports" << std::endl;
        std::cout << "  send <hex bytes>  - Send hex data (e.g., send 01 02 03)" << std::endl;
        std::cout << "  ping              - Send test ping (AA 01 00 01 01 XX XX)" << std::endl;
        std::cout << "  stats             - Show statistics" << std::endl;
        std::cout << "  quit              - Exit" << std::endl;
        std::cout << std::endl;

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "quit" || line == "q") {
                break;
            } else if (line == "ls") {
                print_ports();
            } else if (line == "stats") {
                std::cout << "Bytes received: " << serial.bytes_received() << std::endl;
            } else if (line == "ping") {
                // Simple ping frame (adjust to your protocol)
                std::vector<uint8_t> ping = {0xAA, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00};
                serial.async_write(ping);
                std::cout << "Ping sent" << std::endl;
            } else if (line.substr(0, 5) == "send ") {
                std::vector<uint8_t> data;
                std::istringstream iss(line.substr(5));
                std::string byte_str;
                while (iss >> byte_str) {
                    data.push_back(static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16)));
                }
                if (!data.empty()) {
                    serial.write(data);
                    std::cout << "Sent " << data.size() << " bytes" << std::endl;
                }
            } else if (!line.empty()) {
                std::cout << "Unknown command: " << line << std::endl;
            }
        }

        running = false;
        io.stop();
        io_thread.join();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
