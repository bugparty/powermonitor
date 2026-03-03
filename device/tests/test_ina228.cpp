#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>
#include <cmath>
#include <cstdint>

// Include mock header first to get types and declarations
#include "hardware/i2c.h"

// Mock globals
static i2c_inst_t* mock_i2c_inst = nullptr;
static uint8_t mock_i2c_addr = 0x40;
static float mock_shunt_ohms = 0.015f;

// Capture written data
struct WriteOp {
    uint8_t addr;
    std::vector<uint8_t> payload;
    bool nostop;
};
std::vector<WriteOp> write_ops;

// Data to be returned by next read
static std::vector<uint8_t> next_read_data;

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
        if (!next_read_data.empty()) {
            size_t copy_len = len < next_read_data.size() ? len : next_read_data.size();
            memcpy(dst, next_read_data.data(), copy_len);
            next_read_data.clear();
            return (int)len;
        }
        memset(dst, 0, len);
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

void test_read_burst_data() {
    std::cout << "Testing read_burst_data..." << std::endl;
    INA228 sensor(mock_i2c_inst, mock_i2c_addr, mock_shunt_ohms);

    // Setup mock data for 11 bytes
    // VSHUNT (3 bytes): 0x123450 -> 0x12345 (20 bit) -> +74565
    // VBUS (3 bytes): 0x543210 -> 0x54321 (20 bit) -> 344865
    // DIETEMP (2 bytes): 0x0123 -> +291
    // CURRENT (3 bytes): 0xFEDCB0 -> 0xFEDCB (20 bit, negative)

    next_read_data = {
        0x12, 0x34, 0x50, // VSHUNT
        0x54, 0x32, 0x10, // VBUS
        0x01, 0x23,       // DIETEMP
        0xFE, 0xDC, 0xB0  // CURRENT
    };

    int32_t vshunt, current;
    uint32_t vbus;
    int16_t temp;

    write_ops.clear();
    bool res = sensor.read_burst_data(vshunt, vbus, temp, current);
    assert(res);

    // Verify VSHUNT
    // 0x123450 << 8 -> 0x12345000. >> 12 -> 0x00012345 = 74565
    assert(vshunt == 74565);

    // Verify VBUS
    // 0x543210 >> 4 -> 0x54321 & FFFFF -> 0x54321 = 344865
    assert(vbus == 344865);

    // Verify DIETEMP
    // 0x0123 = 291
    assert(temp == 291);

    // Verify CURRENT
    // 0xFEDCB0 << 8 -> 0xFEDCB000. >> 12 (arithmetic shift)
    // 0xFEDCB000 is negative.
    // Manual: 0xFEDCB = -4661 (in 20-bit 2's complement? wait)
    // 20-bit 0xFEDCB. Top bit is 1 (F=1111).
    // Sign extension should work.
    // 0xFEDCB000 (32-bit) is -19091456
    // >> 12 -> -4661.
    // Let's check: -4661 = 0xFFFFEDCB.
    // 0xFEDCB000 >> 12 = 0xFFFFEDCB. Correct.
    assert(current == -4661);

    std::cout << "read_burst_data: PASS" << std::endl;
}

int main() {
    test_set_shunt_overvoltage();
    test_set_shunt_undervoltage();
    test_read_burst_data();
    return 0;
}
