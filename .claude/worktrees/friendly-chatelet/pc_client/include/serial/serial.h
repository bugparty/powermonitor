#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace serial {

struct PortInfo {
    std::string port;
    std::string description;
    std::string hardware_id;
    std::string manufacturer;
};

std::vector<PortInfo> list_ports();

struct Timeout {
    uint32_t read_timeout_ms = 0;

    static Timeout simpleTimeout(uint32_t timeout_ms) {
        Timeout t;
        t.read_timeout_ms = timeout_ms;
        return t;
    }
};

enum class bytesize_t { eightbits = 8 };
enum class parity_t { parity_none = 0 };
enum class stopbits_t { stopbits_one = 1 };
enum class flowcontrol_t { flowcontrol_none = 0 };
enum class dtrcontrol_t { dtr_disable = 0, dtr_enable = 1 };
enum class rtscontrol_t { rts_disable = 0, rts_enable = 1 };

class SerialException : public std::runtime_error {
public:
    explicit SerialException(const std::string &message) : std::runtime_error(message) {}
};

class PortNotOpenedException : public SerialException {
public:
    explicit PortNotOpenedException(const std::string &message) : SerialException(message) {}
};

class Serial {
public:
    using ReadCallback = std::function<void(const uint8_t *, size_t)>;
    using ErrorCallback = std::function<void(const std::string &)>;

    Serial();
    Serial(const std::string &port, uint32_t baudrate, Timeout timeout,
           bytesize_t bytesize = bytesize_t::eightbits,
           parity_t parity = parity_t::parity_none,
           stopbits_t stopbits = stopbits_t::stopbits_one,
           flowcontrol_t flowcontrol = flowcontrol_t::flowcontrol_none,
           dtrcontrol_t dtr = dtrcontrol_t::dtr_disable,
           rtscontrol_t rts = rtscontrol_t::rts_disable);
    ~Serial();

    Serial(const Serial &) = delete;
    Serial &operator=(const Serial &) = delete;

    void setPort(const std::string &port);
    void setBaudrate(uint32_t baudrate);
    void setTimeout(Timeout timeout);
    void open();
    void close();
    bool isOpen() const;
    size_t read(uint8_t *buffer, size_t size);
    size_t write(const std::vector<uint8_t> &data);
    void setDTR(dtrcontrol_t dtr);
    void setRTS(rtscontrol_t rts);

    void start_async_read(ReadCallback callback, ErrorCallback error_callback);
    void stop_async_read();
    void run_io();
    void run_io_for(std::chrono::milliseconds duration);
    void restart_io();
    void stop_io();

private:
    void configure_port();
    void do_async_read();
    size_t read_with_timeout(uint8_t *buffer, size_t size);

    std::string port_;
    uint32_t baudrate_ = 115200;
    Timeout timeout_;
    bool dtr_enabled_ = false;
    bool rts_enabled_ = false;

    class Impl;
    Impl *impl_;
};

}  // namespace serial
