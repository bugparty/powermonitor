#ifndef INA228_HPP
#define INA228_HPP

#include <cstdint>
#include <cstdio>

class INA228 {
public:
    enum class INA228_Register : uint8_t {
        CONFIG = 0x00,
        ADC_CONFIG = 0x01,
        SHUNT_CAL = 0x02,
        SHUNT_TEMPCO = 0x03,
        VSHUNT = 0x04,
        VBUS = 0x05,
        DIETEMP = 0x06,
        CURRENT = 0x07,
        POWER = 0x08,
        ENERGY = 0x09,
        CHARGE = 0x0A,
        DIAG_ALRT = 0x0B,
        SOVL = 0x0C,
        SUVL = 0x0D,
        BOVL = 0x0E,
        BUVL = 0x0F,
        TEMP_LIMIT = 0x10,
        PWR_LIMIT = 0x11,
        MANUFACTURER_ID = 0x3E,
        DEVICE_ID = 0x3F
    };

    INA228(void* i2c_inst, uint8_t addr, float shunt_ohms) {}

    // Method call counters
    mutable int read_vbus_count = 0;
    mutable int read_vshunt_count = 0;
    mutable int read_current_count = 0;
    mutable int read_temp_count = 0;
    mutable int read_diag_alrt_count = 0;

    // Methods used by sampler.hpp
    bool read_vbus_raw(uint32_t& raw20) const {
        read_vbus_count++;
        raw20 = 0x12345;
        return true;
    }
    bool read_vshunt_raw(int32_t& raw20) const {
        read_vshunt_count++;
        raw20 = 0x0ABCDE;
        return true;
    }
    bool read_current_raw(int32_t& raw20) const {
        read_current_count++;
        raw20 = 0x054321;
        return true;
    }
    bool read_temp_raw(int16_t& raw16) const {
        read_temp_count++;
        raw16 = 0x1234;
        return true;
    }
    bool read_diag_alrt(uint16_t& flags) const {
        read_diag_alrt_count++;
        flags = 0;
        return true;
    }

    // Methods used by CommandHandler
    static uint16_t to_bytes16(uint16_t val) { return val; } // Little-endian mock (no swap)

    bool write_register16(INA228_Register reg, uint16_t val) {
        // Implement mock logic if needed, or just return true
        return true;
    }
    bool read_register16(INA228_Register reg, uint16_t& val) const {
        val = 0;
        return true;
    }
    bool read_register24(INA228_Register reg, uint32_t& val) const {
        val = 0;
        return true;
    }
    bool read_register40(INA228_Register reg, uint64_t& val) const {
        val = 0;
        return true;
    }
};

#endif // INA228_HPP
