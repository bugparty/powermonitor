#include "INA228.hpp"

INA228::INA228(i2c_inst_t *i2c_inst, uint8_t i2c_addr, uint16_t shunt_ohms) 
    : i2c(i2c_inst), addr(i2c_addr), shunt_ohms(shunt_ohms) {}

bool INA228::i2c_read_reg_stop(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
    int w = i2c_write_blocking(this->i2c, addr, &reg, 1, false);
    if (w != 1) {
        INA228_PRINTF("write reg ptr failed, w=%d\n", w);
        return false;
    }
    int r = i2c_read_blocking(this->i2c, addr, buf, n, false);
    if (r != (int)n) {
        INA228_PRINTF("read failed, r=%d\n", r);
        return false;
    }
    return r == (int)n;
}

bool INA228::i2c_write_reg_stop(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
    int w = i2c_write_blocking(this->i2c, addr, &reg, 1, false);
    if (w != 1) {
        INA228_PRINTF("write reg ptr failed, w=%d\n", w);
        return false;
    }
    int r = i2c_write_blocking(this->i2c, addr, buf, n, false);
    if (r != (int)n) {
        INA228_PRINTF("write failed, r=%d\n", r);
        return false;
    }
    return r == (int)n;
}

bool INA228::i2c_read_reg_stop_timeout(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
    int w = i2c_write_timeout_us(this->i2c, addr, &reg, 1, false, 20000);
    if (w != 1) {
        INA228_PRINTF("write reg ptr failed, w=%d\n", w);
        return false;
    }
    int r = i2c_read_timeout_us(this->i2c, addr, buf, n, false, 20000);
    if (r != (int)n) {
        INA228_PRINTF("read failed, r=%d\n", r);
        return false;
    }
    return r == (int)n;
}

uint16_t INA228::to_bytes16(uint16_t register_value) {
    uint16_t b = (register_value >> 8) & 0xFF;
    b |= (register_value & 0xFF) << 8;
    return b;
}

bool INA228::write_register16(uint8_t reg, uint16_t register_value) {
    uint8_t b[2];
    b[0] = (register_value >> 8) & 0xFF;
    b[1] = register_value & 0xFF;
    INA228_PRINTF("write reg 0x%02X  --  val 0x%.4X\n", reg, register_value);
    return i2c_write_reg_stop(this->addr, reg, b, 2);
}

bool INA228::read_register16(uint8_t reg, uint16_t &register_value) {
    uint8_t b[2] = {0};
    if (!i2c_read_reg_stop(this->addr, reg, b, 2)) {
        return false;
    }
    register_value = ((uint16_t)b[0] << 8) | b[1];
    return true;
}

bool INA228::read_register24(uint8_t reg, uint32_t &register_value) {
    uint8_t b[3] = {0};
    if (!i2c_read_reg_stop(this->addr, reg, b, 3)) {
        return false;
    }
    register_value = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[2];
    return true;
}

bool INA228::read_register40(uint8_t reg, uint64_t &register_value) {
    uint8_t b[5] = {0};
    if (!i2c_read_reg_stop(this->addr, reg, b, 5)) {
        return false;
    }
    register_value = ((uint64_t)b[0] << 32) | ((uint64_t)b[1] << 24) | ((uint64_t)b[2] << 16) | ((uint64_t)b[3] << 8) | (uint64_t)b[4];
    return true;
}

bool INA228::set_config() {
    uint16_t config = (INA228_CONVERSION_DELAY << INA228_CONVDLY_NBIT) | (INA228_TEMP_COMP << INA228_TEMPCOMP_NBIT) | (INA228_ADCRANGE << INA228_ADCRANGE_NBIT);
    INA228_PRINTF("Config reg value to set: 0x%04X\n", config);
    return read_register16(INA228_CONFIG, config);
}

bool INA228::set_adc_config() {
    uint16_t config = (INA228_ADC_MODE << INA228_ADC_MODE_NBIT) | (INA228_VBUS_CONV_TIME << INA228_VBUS_CONV_TIME_NBIT) | (INA228_VSHCT_CONV_TIME << INA228_VSHCT_CONV_TIME_NBIT) | (INA228_VTCT_CONV_TIME << INA228_VTCT_CONV_TIME_NBIT) | (INA228_ADC_AVG << INA228_AVG_NBIT);
    INA228_PRINTF("ADC Config reg value to set: 0x%04X\n", config);
    return write_register16(INA228_ADC_CONFIG, config);
}

float INA228::get_current_lsb() {
    float temp;
    if (INA228_ADCRANGE == 0) {
        temp = 163.84e-3;
    } else {
        temp = 40.96e-3;
    }
    float current_lsb = (temp / this->shunt_ohms) / 524288.0;
    return current_lsb;
}

bool INA228::shunt_calib() {
    float current_lsb = get_current_lsb();
    uint16_t calib_value = (uint16_t)(13107.2e6 * current_lsb * this->shunt_ohms);
    INA228_PRINTF("Shunt calibration value: %d\n", calib_value);
    return write_register16(INA228_SHUNT_CAL, calib_value);
}

bool INA228::shunt_tempco() {
    INA228_PRINTF("Setting shunt temperature compensation value\n");
    return write_register16(INA228_SHUNT_TEMPCO, INA228_SHUNT_TEMPCO_VALUE);
}

bool INA228::reset_all() {
    uint16_t config = 1 << INA228_RST_NBIT;
    INA228_PRINTF("Resetting all (config=0x%04X)\n", config);
    return write_register16(INA228_CONFIG, config);
}

bool INA228::reset_energy() {
    uint16_t config = 1 << INA228_RSTACC_NBIT;
    INA228_PRINTF("Resetting energy/charge accumulation (config=0x%04X)\n", config);
    return write_register16(INA228_CONFIG, config);
}

float INA228::convert2comp2float(int64_t twocompdata, uint8_t nrofbit, float factor) {
    int64_t isnegative = 1LL << (nrofbit - 1);
    if (twocompdata >= isnegative) {
        twocompdata = (twocompdata - (2 * isnegative)) * factor;
    } else {
        twocompdata = twocompdata * factor;
    }
    return (float)twocompdata;
}

float INA228::get_energy() {
    uint64_t raw = 0;
    if (!read_register40(INA228_ENERGY, raw)) {
        INA228_PRINTF("Failed to read energy register\n");
        return 0.0;
    }
    float current_lsb = get_current_lsb();
    float energy = raw * 3.2 * 16.0 * current_lsb;
    INA228_PRINTF("Energy raw=0x%llX, value=%f\n", raw, energy);
    return energy;
}

float INA228::get_power() {
    uint32_t raw = 0;
    if (!read_register24(INA228_POWER, raw)) {
        INA228_PRINTF("Failed to read power register\n");
        return 0.0;
    }
    float current_lsb = get_current_lsb();
    float power = 3.2 * raw * current_lsb;
    INA228_PRINTF("Power raw=0x%06X, value=%f\n", raw, power);
    return power;
}

float INA228::get_temp() {
    uint16_t raw = 0;
    if (!read_register16(INA228_DIETEMP, raw)) {
        INA228_PRINTF("Failed to read temperature register\n");
        return 0.0;
    }
    float conversion_factor = 7.8125e-3;
    float temp = convert2comp2float((int64_t)raw, 16, conversion_factor);
    INA228_PRINTF("Temperature raw=0x%04X, value=%f°C\n", raw, temp);
    return temp;
}

float INA228::get_vbus() {
    uint32_t raw = 0;
    if (!read_register24(INA228_VBUS, raw)) {
        INA228_PRINTF("Failed to read VBUS register\n");
        return 0.0;
    }
    float conversion_factor = 195.3125e-6;
    float vbus = convert2comp2float((int64_t)(raw >> 4), 20, conversion_factor);
    INA228_PRINTF("VBUS raw=0x%06X (shifted=0x%05X), value=%f V\n", raw, raw >> 4, vbus);
    return vbus;
}

float INA228::get_current() {
    uint32_t raw = 0;
    if (!read_register24(INA228_CURRENT, raw)) {
        INA228_PRINTF("Failed to read current register\n");
        return 0.0;
    }
    float current_lsb = get_current_lsb();
    float current = convert2comp2float((int64_t)(raw >> 4), 20, current_lsb);
    INA228_PRINTF("Current raw=0x%06X (shifted=0x%05X), value=%f A\n", raw, raw >> 4, current);
    return current;
}

float INA228::get_charge() {
    uint64_t raw = 0;
    if (!read_register40(INA228_CHARGE, raw)) {
        INA228_PRINTF("Failed to read charge register\n");
        return 0.0;
    }
    float charge = convert2comp2float((int64_t)raw, 40, 1.0);
    INA228_PRINTF("Charge raw=0x%llX, value=%f\n", raw, charge);
    return charge;
}

void INA228::print_manufacturer_id() {
    uint16_t raw_id = 0;
    if (!read_register16(INA228_MANUFACTURER_ID, raw_id)) {
        INA228_PRINTF("Failed to read manufacturer ID\n");
        return;
    }
    uint8_t first_byte = (raw_id >> 8) & 0xFF;
    uint8_t second_byte = raw_id & 0xFF;
    printf("Manufacturer ID (HEX): 0x%04X\n", raw_id);
    printf("Manufacturer ID (CHAR): %c%c\n", first_byte, second_byte);
}

void INA228::print_device_id() {
    uint16_t raw_id = 0;
    if (!read_register16(INA228_DEVICE_ID, raw_id)) {
        INA228_PRINTF("Failed to read device ID\n");
        return;
    }
    uint16_t device_id = (raw_id >> 4) & 0xFFF;
    uint8_t revision = raw_id & 0x0F;
    printf("Device ID: 0x%03X\n", device_id);
    printf("Revision: 0x%X\n", revision);
}

float INA228::get_shunt_conv_factor() {
    if (INA228_ADCRANGE == 0) {
        return 163.84e-3 / 32768.0;
    } else {
        return 40.96e-3 / 32768.0;
    }
}

void INA228::get_diag_alerts(uint8_t alert) {
    uint16_t raw = 0;
    if (!read_register16(INA228_DIAG_ALRT, raw)) {
        printf("Failed to read DIAG_ALRT register\n");
        return;
    }
    
    switch(alert) {
        case INA228_ALERT_MEMSTAT:
            if ((raw & 0x0001) == 0x0000) {
                printf("MEMSTAT: Checksum error is detected in the device trim memory space\n");
            }
            break;
        case INA228_ALERT_CNVRF:
            if ((raw & 0x0002) == 0x0002) {
                printf("CNVRF: Conversion is completed\n");
            }
            break;
        case INA228_ALERT_BUSUL:
            if ((raw & 0x0004) == 0x0004) {
                printf("BUSUL: Bus voltage measurement falls below the threshold limit\n");
            }
            break;
        case INA228_ALERT_BUSOL:
            if ((raw & 0x0008) == 0x0008) {
                printf("BUSOL: Bus voltage measurement exceeds the threshold limit\n");
            }
            break;
        case INA228_ALERT_SHNTUL:
            if ((raw & 0x0010) == 0x0010) {
                printf("SHNTUL: Shunt voltage measurement falls below the threshold limit\n");
            }
            break;
        case INA228_ALERT_SHNTOL:
            if ((raw & 0x0040) == 0x0040) {
                printf("SHNTOL: Shunt voltage measurement exceeds the threshold limit\n");
            }
            break;
        case INA228_ALERT_TMPOL:
            if ((raw & 0x0080) == 0x0080) {
                printf("TMPOL: Temperature measurement exceeds the threshold limit\n");
            }
            break;
        case INA228_ALERT_MATHOF:
            if ((raw & 0x0100) == 0x0100) {
                printf("MATHOF: Arithmetic operation resulted in an overflow error\n");
            }
            break;
        case INA228_ALERT_CHARGEOF:
            if ((raw & 0x0200) == 0x0200) {
                printf("CHARGEOF: 40 bit CHARGE register has overflowed\n");
            }
            break;
        case INA228_ALERT_ENERGYOF:
            if ((raw & 0x0400) == 0x0400) {
                printf("ENERGYOF: 40 bit ENERGY register has overflowed\n");
            }
            break;
        case INA228_ALERT_APOL:
            if ((raw & 0x0800) == 0x0800) {
                printf("APOL: Alert pin polarity inverted (active-high, open-drain)\n");
            } else {
                printf("APOL: Alert pin polarity normal (active-low, open-drain)\n");
            }
            break;
        case INA228_ALERT_SLOWALERT:
            if ((raw & 0x2000) == 0x2000) {
                printf("SLOWALERT: ALERT comparison on averaged value\n");
            } else {
                printf("SLOWALERT: ALERT comparison on non-averaged (ADC) value\n");
            }
            break;
        case INA228_ALERT_CNVR:
            if ((raw & 0x4000) == 0x4000) {
                printf("CNVR: Conversion ready flag enabled on ALERT pin\n");
            } else {
                printf("CNVR: Conversion ready flag disabled on ALERT pin\n");
            }
            break;
        case INA228_ALERT_ALATCH:
            if ((raw & 0x8000) == 0x8000) {
                printf("ALATCH: Latched\n");
            } else {
                printf("ALATCH: Transparent\n");
            }
            break;
    }
}

bool INA228::set_shunt_overvoltage(float value) {
    uint16_t data;
    float conv_factor = get_shunt_conv_factor();
    
    if (value >= 0) {
        data = (uint16_t)((value * this->shunt_ohms) / conv_factor);
    } else {
        float value_temp = value * (-1.0);
        int16_t temp = (int16_t)((value_temp * this->shunt_ohms) / conv_factor);
        data = (uint16_t)(~temp + 1);
    }
    
    INA228_PRINTF("Setting shunt overvoltage threshold: %f (data=0x%04X)\n", value, data);
    return write_register16(INA228_SOVL, data);
}

bool INA228::set_shunt_undervoltage(float value) {
    uint16_t data;
    float conv_factor = get_shunt_conv_factor();
    
    if (value >= 0) {
        data = (uint16_t)((value * this->shunt_ohms) / conv_factor);
    } else {
        float value_temp = value * (-1.0);
        int16_t temp = (int16_t)((value_temp * this->shunt_ohms) / conv_factor);
        data = (uint16_t)(~temp + 1);
    }
    
    INA228_PRINTF("Setting shunt undervoltage threshold: %f (data=0x%04X)\n", value, data);
    return write_register16(INA228_SUVL, data);
}

bool INA228::set_bus_overvoltage(float value) {
    uint16_t data = (uint16_t)(value / (16.0 * 195.3125e-6));
    INA228_PRINTF("Setting bus overvoltage threshold: %f V (data=0x%04X)\n", value, data);
    return write_register16(INA228_BOVL, data);
}

bool INA228::set_bus_undervoltage(float value) {
    uint16_t data = (uint16_t)(value / (16.0 * 195.3125e-6));
    INA228_PRINTF("Setting bus undervoltage threshold: %f V (data=0x%04X)\n", value, data);
    return write_register16(INA228_BUVL, data);
}

bool INA228::set_temp_limit(float value, float consta) {
    uint16_t data = (uint16_t)(value / (16.0 * consta));
    INA228_PRINTF("Setting temperature limit: %f (data=0x%04X)\n", value, data);
    return write_register16(INA228_TEMP_LIMIT, data);
}

bool INA228::set_power_overlimit(float value, float consta) {
    uint16_t data = (uint16_t)(value / (16.0 * consta));
    INA228_PRINTF("Setting power overlimit: %f (data=0x%04X)\n", value, data);
    return write_register16(INA228_PWR_LIMIT, data);
}

void INA228::configure() {
    set_config();
    sleep_ms(10);
    set_adc_config();
    sleep_ms(10);
    shunt_calib();
    sleep_ms(10);
    shunt_tempco();
    sleep_ms(10);
    reset_energy();
    sleep_ms(10);
}
