#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <deque>

// Include mock header first to get types and declarations
#include "hardware/i2c.h"

// Mock globals
static i2c_inst_t* mock_i2c_inst = nullptr;
static uint8_t mock_i2c_addr = 0x40;
static float mock_shunt_ohms = 0.015f;
static std::deque<uint8_t> mock_read_data;

// Capture written data
struct WriteOp {
    uint8_t addr;
    std::vector<uint8_t> payload;
    bool nostop;
};
std::vector<WriteOp> write_ops;

// Implement I2C mocks
extern "C" {
    int i2c_write_blocking(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop) {
        if (len > 0) {
            std::vector<uint8_t> p(src, src + len);
            write_ops.push_back({addr, p, nostop});
        }
        return (int)len;
    }
    int i2c_read_blocking(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop) {
        if (!mock_read_data.empty()) {
            for (size_t i = 0; i < len; ++i) {
                if (mock_read_data.empty()) {
                    dst[i] = 0;
                } else {
                    dst[i] = mock_read_data.front();
                    mock_read_data.pop_front();
                }
            }
        } else {
            memset(dst, 0, len);
        }
        return (int)len;
    }
    int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop, uint64_t timeout_us) {
        return i2c_write_blocking(i2c, addr, src, len, nostop);
    }
    int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop, uint64_t timeout_us) {
        return i2c_read_blocking(i2c, addr, dst, len, nostop);
    }
}

// Include real INA228 header
#include "../INA228.hpp"

void test_set_shunt_overvoltage() {
    std::cout << "Testing set_shunt_overvoltage..." << std::endl;
    INA228 sensor(mock_i2c_inst, mock_i2c_addr, mock_shunt_ohms);

    // Clear ops
    write_ops.clear();

    // Test positive value
    float val_pos = 0.1f; // 100mV
    if (!sensor.set_shunt_overvoltage(val_pos)) {
        std::cerr << "set_shunt_overvoltage failed" << std::endl;
        assert(false);
    }

    // Expecting read of CONFIG (len 1) and write of SOVL (len 3)
    // But since get_config calls read_register16 which calls i2c_read_reg_stop which calls i2c_write_blocking(1 byte).
    // So we expect at least 1 write before the main write.
    // Let's just look at the last write or the one with 3 bytes.
    bool found = false;
    uint16_t written_pos = 0;
    for (const auto& op : write_ops) {
        if (op.payload.size() == 3 && op.payload[0] == (uint8_t)INA228::INA228_Register::SOVL) {
            written_pos = (op.payload[1] << 8) | op.payload[2];
            found = true;
            break;
        }
    }
    if (!found) {
        std::cerr << "SOVL write not found. Ops count: " << write_ops.size() << std::endl;
        for (const auto& op : write_ops) {
            std::cerr << "Op len: " << op.payload.size() << " Addr: " << (int)op.payload[0] << std::endl;
        }
        assert(false);
    }

    std::cout << "Positive value written: 0x" << std::hex << written_pos << std::dec << std::endl;

    // Test negative value
    write_ops.clear();
    float val_neg = -0.1f; // -100mV
    if (!sensor.set_shunt_overvoltage(val_neg)) {
        std::cerr << "set_shunt_overvoltage failed" << std::endl;
        assert(false);
    }

    found = false;
    uint16_t written_neg = 0;
    for (const auto& op : write_ops) {
        if (op.payload.size() == 3 && op.payload[0] == (uint8_t)INA228::INA228_Register::SOVL) {
            written_neg = (op.payload[1] << 8) | op.payload[2];
            found = true;
            break;
        }
    }
    assert(found);
    std::cout << "Negative value written: 0x" << std::hex << written_neg << std::dec << std::endl;

    // written_neg should be 2's complement of written_pos (roughly)
    // written_pos + written_neg should be 0 (mod 65536)
    uint16_t sum = written_pos + written_neg;
    assert(sum == 0);
    std::cout << "Symmetry check: PASS" << std::endl;
}

void test_set_shunt_undervoltage() {
    std::cout << "Testing set_shunt_undervoltage..." << std::endl;
    INA228 sensor(mock_i2c_inst, mock_i2c_addr, mock_shunt_ohms);

    write_ops.clear();
    float val_pos = 0.05f;
    if (!sensor.set_shunt_undervoltage(val_pos)) {
        std::cerr << "set_shunt_undervoltage failed" << std::endl;
        assert(false);
    }

    bool found = false;
    uint16_t written_pos = 0;
    for (const auto& op : write_ops) {
        if (op.payload.size() == 3 && op.payload[0] == (uint8_t)INA228::INA228_Register::SUVL) {
            written_pos = (op.payload[1] << 8) | op.payload[2];
            found = true;
            break;
        }
    }
    assert(found);

    write_ops.clear();
    float val_neg = -0.05f;
    if (!sensor.set_shunt_undervoltage(val_neg)) {
        std::cerr << "set_shunt_undervoltage failed" << std::endl;
        assert(false);
    }

    found = false;
    uint16_t written_neg = 0;
    for (const auto& op : write_ops) {
        if (op.payload.size() == 3 && op.payload[0] == (uint8_t)INA228::INA228_Register::SUVL) {
            written_neg = (op.payload[1] << 8) | op.payload[2];
            found = true;
            break;
        }
    }
    assert(found);

    assert((uint16_t)(written_pos + written_neg) == 0);
    std::cout << "Symmetry check: PASS" << std::endl;
}

void test_read_burst_sample() {
    std::cout << "Testing read_burst_sample..." << std::endl;
    INA228 sensor(mock_i2c_inst, mock_i2c_addr, mock_shunt_ohms);

    // Prepare mock data for 11 bytes
    // VSHUNT: 3 bytes. Value: 0x123450 (20 bits top aligned) -> 0x12345
    // VBUS: 3 bytes. Value: 0x543210 (20 bits top aligned) -> 0x54321
    // TEMP: 2 bytes. Value: 0xABCD -> -21555
    // CURRENT: 3 bytes. Value: 0xFEDCBA (20 bits top aligned) -> -74566 (negative)

    mock_read_data.clear();
    // VSHUNT: 0x12, 0x34, 0x50
    mock_read_data.push_back(0x12); mock_read_data.push_back(0x34); mock_read_data.push_back(0x50);
    // VBUS: 0x54, 0x32, 0x10
    mock_read_data.push_back(0x54); mock_read_data.push_back(0x32); mock_read_data.push_back(0x10);
    // TEMP: 0xAB, 0xCD
    mock_read_data.push_back(0xAB); mock_read_data.push_back(0xCD);
    // CURRENT: 0xFE, 0xDC, 0xB0 (last nibble 0 to test alignment, though B0 is fine)
    // 0xFEDCB0 << 8 = 0xFEDCB000.
    // 0xFEDCB000 >> 12.
    mock_read_data.push_back(0xFE); mock_read_data.push_back(0xDC); mock_read_data.push_back(0xB0);

    int32_t vshunt = 0, current = 0;
    uint32_t vbus = 0;
    int16_t temp = 0;

    if (!sensor.read_burst_sample(vshunt, vbus, temp, current)) {
        std::cerr << "read_burst_sample failed" << std::endl;
        assert(false);
    }

    // VSHUNT: 0x123450 << 8 = 0x12345000. >> 12 = 0x00012345.
    std::cout << "VSHUNT: 0x" << std::hex << vshunt << " (expected 0x12345)" << std::dec << std::endl;
    assert(vshunt == 0x12345);

    // VBUS: 0x543210 >> 4 = 0x54321.
    std::cout << "VBUS: 0x" << std::hex << vbus << " (expected 0x54321)" << std::dec << std::endl;
    assert(vbus == 0x54321);

    // TEMP: 0xABCD
    std::cout << "TEMP: " << (int)temp << " (expected " << (int16_t)0xABCD << ")" << std::endl;
    assert(temp == (int16_t)0xABCD);

    // CURRENT: 0xFEDCB000 >> 12
    int32_t c_expected = (int32_t)0xFEDCB000 >> 12;
    std::cout << "CURRENT: 0x" << std::hex << current << " (expected 0x" << c_expected << ")" << std::dec << std::endl;
    assert(current == c_expected);

    std::cout << "Burst read test passed!" << std::endl;
}

int main() {
    test_set_shunt_overvoltage();
    test_set_shunt_undervoltage();
    test_read_burst_sample();
    return 0;
}
