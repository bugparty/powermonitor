#pragma once
#include <cstdint>

// Forward declare register enum to avoid including real INA228.hpp
class INA228 {
public:
    enum class INA228_Register : uint8_t {
        CONFIG = 0,
        ADC_CONFIG = 1,
        SHUNT_CAL = 2,
        SHUNT_TEMPCO = 3,
        VSHUNT = 4,
        VBUS = 5,
        DIETEMP = 6,
        CURRENT = 7,
        POWER = 8,
        ENERGY = 9,
        CHARGE = 10,
        DIAG_ALRT = 11,
        SOVL = 12,
        SUVL = 13,
        BOVL = 14,
        BUVL = 15,
        TEMP_LIMIT = 16,
        PWR_LIMIT = 17,
        MANUFACTURER_ID = 0x3E,
        DEVICE_ID = 0x3F,
    };

    INA228(void* i2c_inst, uint8_t addr, float shunt_ohms) {}

    // Mock methods used by CommandHandler
    bool write_register16(INA228_Register reg, uint16_t val) { return true; }
    bool read_register16(INA228_Register reg, uint16_t& val) { val = 0; return true; }
    bool read_register24(INA228_Register reg, uint32_t& val) { val = 0; return true; }
    bool read_register40(INA228_Register reg, uint64_t& val) { val = 0; return true; }

    // Mock methods used by Sampler
    bool read_diag_alrt(uint16_t& val) { val = 0; return true; }
    bool read_vbus_raw(uint32_t& val) { val = 0; return true; }
    bool read_vshunt_raw(int32_t& val) { val = 0; return true; }
    bool read_current_raw(int32_t& val) { val = 0; return true; }
    bool read_temp_raw(int16_t& val) { val = 0; return true; }

    static uint16_t to_bytes16(uint16_t val) { return val; }
};
