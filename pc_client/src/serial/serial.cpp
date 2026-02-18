#include "serial/serial.h"

#include "serial/serial.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <system_error>

#include <asio.hpp>

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#pragma comment(lib, "setupapi.lib")
#else
#include <dirent.h>
#include <cstring>
#include <sys/ioctl.h>
#include <termios.h>
#endif

namespace serial {

namespace {
constexpr size_t kAsyncReadBufferSize = 3;

#ifdef _WIN32
std::string get_registry_property(HDEVINFO hDevInfo, SP_DEVINFO_DATA *devInfoData, DWORD property) {
    char buffer[512] = {0};
    if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, devInfoData, property, nullptr,
                                          reinterpret_cast<PBYTE>(buffer), sizeof(buffer),
                                          nullptr)) {
        return std::string(buffer);
    }
    return "";
}
#else
std::string read_sysfs(const std::string &path) {
    std::ifstream file(path);
    if (file) {
        std::string content;
        std::getline(file, content);
        return content;
    }
    return "";
}
#endif

std::string ensure_dev_prefix(const std::string &port) {
#ifdef _WIN32
    return port;
#else
    if (port.rfind("/dev/", 0) == 0) {
        return port;
    }
    return "/dev/" + port;
#endif
}
}  // namespace

std::vector<PortInfo> list_ports() {
    std::vector<PortInfo> ports;

#ifdef _WIN32
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return ports;
    }

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
        PortInfo info;
        std::string friendlyName = get_registry_property(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
        if (friendlyName.empty()) {
            continue;
        }
        size_t start = friendlyName.find("(COM");
        size_t end = friendlyName.find(")", start);
        if (start == std::string::npos || end == std::string::npos) {
            continue;
        }
        info.port = friendlyName.substr(start + 1, end - start - 1);
        info.description = get_registry_property(hDevInfo, &devInfoData, SPDRP_DEVICEDESC);
        info.manufacturer = get_registry_property(hDevInfo, &devInfoData, SPDRP_MFG);

        std::string hwid = get_registry_property(hDevInfo, &devInfoData, SPDRP_HARDWAREID);
        info.hardware_id = hwid;
 
        ports.push_back(info);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    std::sort(ports.begin(), ports.end(), [](const PortInfo &a, const PortInfo &b) {
        auto getNum = [](const std::string &s) {
            size_t pos = s.find("COM");
            if (pos != std::string::npos) {
                try {
                    return std::stoi(s.substr(pos + 3));
                } catch (...) {
                }
            }
            return 0;
        };
        return getNum(a.port) < getNum(b.port);
    });
#else
    DIR *dir = opendir("/dev");
    if (!dir) {
        return ports;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find("ttyUSB") == 0 || name.find("ttyACM") == 0) {
            PortInfo info;
            info.port = "/dev/" + name;

            std::string sysfs_base = "/sys/class/tty/" + name + "/device";
            std::string usb_base;
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

    std::sort(ports.begin(), ports.end(), [](const PortInfo &a, const PortInfo &b) {
        return a.port < b.port;
    });
#endif

    return ports;
}

class Serial::Impl {
public:
    Impl() : io_(), serial_(io_), timer_(io_) {}

    asio::io_context io_;
    asio::serial_port serial_;
    asio::steady_timer timer_;
    std::mutex mutex_;
    std::array<uint8_t, kAsyncReadBufferSize> read_buf_{};
    ReadCallback read_cb_{};
    ErrorCallback error_cb_{};
    std::atomic<bool> async_reading_{false};
};

Serial::Serial() : impl_(new Impl()) {}

Serial::Serial(const std::string &port, uint32_t baudrate, Timeout timeout, bytesize_t,
               parity_t, stopbits_t, flowcontrol_t, dtrcontrol_t dtr, rtscontrol_t rts)
    : port_(ensure_dev_prefix(port)),
      baudrate_(baudrate),
      timeout_(timeout),
      dtr_enabled_(dtr == dtrcontrol_t::dtr_enable),
      rts_enabled_(rts == rtscontrol_t::rts_enable),
      impl_(new Impl()) {
    open();
}

Serial::~Serial() {
    close();
    delete impl_;
}

void Serial::setPort(const std::string &port) {
    port_ = ensure_dev_prefix(port);
}

void Serial::setBaudrate(uint32_t baudrate) {
    baudrate_ = baudrate;
    if (isOpen()) {
        configure_port();
    }
}

void Serial::setTimeout(Timeout timeout) {
    timeout_ = timeout;
}

void Serial::open() {
    if (port_.empty()) {
        throw SerialException("Port is not specified");
    }
    asio::error_code ec;
    impl_->serial_.open(port_, ec);
    if (ec) {
        throw SerialException("Failed to open port: " + ec.message());
    }
    configure_port();
}

void Serial::close() {
    asio::error_code ec;
    if (impl_->serial_.is_open()) {
        impl_->serial_.cancel(ec);
        impl_->serial_.close(ec);
    }
}

bool Serial::isOpen() const {
    return impl_->serial_.is_open();
}

size_t Serial::read(uint8_t *buffer, size_t size) {
    if (!isOpen()) {
        throw PortNotOpenedException("Serial port not opened");
    }
    return read_with_timeout(buffer, size);
}

size_t Serial::write(const std::vector<uint8_t> &data) {
    if (!isOpen()) {
        throw PortNotOpenedException("Serial port not opened");
    }
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    asio::error_code ec;
    size_t bytes = asio::write(impl_->serial_, asio::buffer(data), ec);
    if (ec) {
        throw SerialException("Write error: " + ec.message());
    }
    return bytes;
}

void Serial::setDTR(dtrcontrol_t dtr) {
    dtr_enabled_ = (dtr == dtrcontrol_t::dtr_enable);
}

void Serial::setRTS(rtscontrol_t rts) {
    rts_enabled_ = (rts == rtscontrol_t::rts_enable);
}

void Serial::start_async_read(ReadCallback callback, ErrorCallback error_callback) {
    if (!isOpen()) {
        throw PortNotOpenedException("Serial port not opened");
    }
    impl_->read_cb_ = std::move(callback);
    impl_->error_cb_ = std::move(error_callback);
    impl_->async_reading_.store(true);
    do_async_read();
}

void Serial::stop_async_read() {
    impl_->async_reading_.store(false);
    asio::error_code ec;
    impl_->serial_.cancel(ec);
}

void Serial::run_io() {
    impl_->io_.run();
}

void Serial::run_io_for(std::chrono::milliseconds duration) {
    impl_->io_.run_for(duration);
}

void Serial::restart_io() {
    impl_->io_.restart();
}

void Serial::stop_io() {
    impl_->io_.stop();
}

void Serial::do_async_read() {
    if (!impl_->async_reading_.load()) {
        return;
    }
    impl_->serial_.async_read_some(
        asio::buffer(impl_->read_buf_),
        [this](const asio::error_code &ec, size_t bytes) {
            if (!ec) {
                if (impl_->read_cb_) {
                    impl_->read_cb_(impl_->read_buf_.data(), bytes);
                }
                do_async_read();
            } else if (ec != asio::error::operation_aborted) {
                if (impl_->error_cb_) {
                    impl_->error_cb_(ec.message());
                }
            }
        });
}

void Serial::configure_port() {
    asio::error_code ec;
    impl_->serial_.set_option(asio::serial_port_base::baud_rate(baudrate_), ec);
    if (ec) {
        throw SerialException("Failed to set baud rate: " + ec.message());
    }
    impl_->serial_.set_option(asio::serial_port_base::character_size(8), ec);
    impl_->serial_.set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::none),
                              ec);
    impl_->serial_.set_option(asio::serial_port_base::stop_bits(asio::serial_port_base::stop_bits::one),
                              ec);
    impl_->serial_.set_option(
        asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::none), ec);
    
    // DTR/RTS control
#ifdef _WIN32
    HANDLE handle = impl_->serial_.native_handle();
    if (dtr_enabled_) {
        EscapeCommFunction(handle, SETDTR);
    } else {
        EscapeCommFunction(handle, CLRDTR);
    }
    if (rts_enabled_) {
        EscapeCommFunction(handle, SETRTS);
    } else {
        EscapeCommFunction(handle, CLRRTS);
    }
#else
    int handle = impl_->serial_.native_handle();
    int status;
    if (ioctl(handle, TIOCMGET, &status) == 0) {
        if (dtr_enabled_) {
            status |= TIOCM_DTR;
        } else {
            status &= ~TIOCM_DTR;
        }
        if (rts_enabled_) {
            status |= TIOCM_RTS;
        } else {
            status &= ~TIOCM_RTS;
        }
        ioctl(handle, TIOCMSET, &status);
    }
#endif
}

size_t Serial::read_with_timeout(uint8_t *buffer, size_t size) {
    if (timeout_.read_timeout_ms == 0) {
        asio::error_code ec;
        size_t bytes = impl_->serial_.read_some(asio::buffer(buffer, size), ec);
        if (ec) {
            throw SerialException("Read error: " + ec.message());
        }
        return bytes;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex_);

    size_t bytes = 0;
    asio::error_code read_ec;
    bool done = false;

    impl_->timer_.expires_after(std::chrono::milliseconds(timeout_.read_timeout_ms));
    impl_->timer_.async_wait([this, &done, &read_ec](const asio::error_code &ec) {
        if (!ec && !done) {
            read_ec = asio::error::timed_out;
            impl_->serial_.cancel();
        }
    });

    impl_->serial_.async_read_some(asio::buffer(buffer, size),
                                   [&done, &read_ec, &bytes](const asio::error_code &ec,
                                                            size_t n) {
                                       done = true;
                                       read_ec = ec;
                                       bytes = n;
                                   });

    impl_->io_.restart();
    impl_->io_.run();

    if (read_ec == asio::error::timed_out) {
        return 0;
    }
    if (read_ec && read_ec != asio::error::operation_aborted) {
        throw SerialException("Read error: " + read_ec.message());
    }
    return bytes;
}

}  // namespace serial
