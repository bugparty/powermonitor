#include "INA228.hpp"

INA228::INA228(i2c_inst_t *i2c_inst, uint8_t i2c_addr, float shunt_ohms, float max_current) 
    : i2c_(i2c_inst), addr_(i2c_addr), shunt_ohms_(shunt_ohms), max_current_(max_current) {}

bool INA228::i2c_read_reg_stop(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) const {
    int w = i2c_write_blocking(this->i2c_, addr, &reg, 1, true);
    if (w != 1) {
        detail::DEBUG_PRINTF("write reg ptr failed, w=%d\n", w);
        return false;
    }
    int r = i2c_read_blocking(this->i2c_, addr, buf, n, false);
    if (r != (int)n) {
        detail::DEBUG_PRINTF("read failed, r=%d\n", r);
        return false;
    }
    return r == (int)n;
}

bool INA228::i2c_write_reg_stop(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) const {
    int w = i2c_write_blocking(this->i2c_, addr, &reg, 1, true);
    if (w != 1) {
        detail::DEBUG_PRINTF("write reg ptr failed, w=%d\n", w);
        return false;
    }
    int r = i2c_write_blocking(this->i2c_, addr, buf, n, false);
    if (r != (int)n) {
        detail::DEBUG_PRINTF("write failed, r=%d\n", r);
        return false;
    }
    return r == (int)n;
}

bool INA228::i2c_read_reg_stop_timeout(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
    int w = i2c_write_timeout_us(this->i2c_, addr, &reg, 1, false, 20000);
    if (w != 1) {
        detail::DEBUG_PRINTF("write reg ptr failed, w=%d\n", w);
        return false;
    }
    int r = i2c_read_timeout_us(this->i2c_, addr, buf, n, false, 20000);
    if (r != (int)n) {
        detail::DEBUG_PRINTF("read failed, r=%d\n", r);
        return false;
    }
    return r == (int)n;
}

constexpr uint16_t INA228::to_bytes16(uint16_t register_value) noexcept {
    uint16_t b = (register_value >> 8) & 0xFF;
    b |= (register_value & 0xFF) << 8;
    return b;
}

bool INA228::write_register16(INA228_Register reg, uint16_t register_value) {
    uint8_t b[3];
    b[0] = static_cast<uint8_t>(reg);
    b[1] = (register_value >> 8) & 0xFF;
    b[2] = register_value & 0xFF;
    detail::DEBUG_PRINTF("write reg 0x%02X  --  val 0x%.4X\n", reg, register_value);
    int w = i2c_write_blocking(this->i2c_, addr_, b, 3, false);
    if (w != 3) {
        detail::DEBUG_PRINTF("write reg ptr failed, w=%d\n", w);
        return false;
    }
    return true;
}

bool INA228::read_register16(INA228_Register reg, uint16_t &register_value) const {
    uint8_t b[2] = {0};
    if (!i2c_read_reg_stop(this->addr_, static_cast<uint8_t>(reg), b, 2)) {
        return false;
    }
    register_value = ((uint16_t)b[0] << 8) | b[1];
    return true;
}

bool INA228::read_register24(INA228_Register reg, uint32_t &register_value) const {
    uint8_t b[3] = {0};
    if (!i2c_read_reg_stop(this->addr_, static_cast<uint8_t>(reg), b, 3)) {
        return false;
    }
    register_value = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[2];
    return true;
}

bool INA228::read_register40(INA228_Register reg, uint64_t &register_value) const {
    uint8_t b[5] = {0};
    if (!i2c_read_reg_stop(this->addr_, static_cast<uint8_t>(reg), b, 5)) {
        return false;
    }
    register_value = ((uint64_t)b[0] << 32) | ((uint64_t)b[1] << 24) | ((uint64_t)b[2] << 16) | ((uint64_t)b[3] << 8) | (uint64_t)b[4];
    return true;
}

bool INA228::set_config() {
    uint16_t config = (CONVERSION_DELAY << CONVDLY_NBIT) | (TEMP_COMP << TEMPCOMP_NBIT) | (ADCRANGE << ADCRANGE_NBIT);
    detail::DEBUG_PRINTF("Config reg value to set: 0x%04X\n", config);
    return write_register16(INA228_Register::CONFIG, config);
}
bool INA228::get_config(uint16_t &config) const {
    if (!read_register16(INA228_Register::CONFIG, config)) {
        detail::DEBUG_PRINTF("Failed to read CONFIG register\n");
        return false;
    }
    detail::DEBUG_PRINTF("Config reg value read: 0x%04X\n", config);
    return true;
}
bool INA228::set_adc_config() {
    uint16_t config = (ADC_MODE << ADC_MODE_NBIT) | (VBUS_CONV_TIME << VBUS_CONV_TIME_NBIT) | (VSHCT_CONV_TIME << VSHCT_CONV_TIME_NBIT) | (VTCT_CONV_TIME << VTCT_CONV_TIME_NBIT) | (ADC_AVG << AVG_NBIT);
    detail::DEBUG_PRINTF("ADC Config reg value to set: 0x%04X\n", config);
    return write_register16(INA228_Register::ADC_CONFIG, config);
}
/**************************************************************************/
/*!
    @brief Reads the shunt full scale ADC range across IN+ and IN-.
    @return Shunt full scale ADC range (0: +/-163.84 mV or 1: +/-40.96 mV)
*/
/**************************************************************************/
uint8_t INA228::get_adc_range() const {
    uint16_t config;
    if (!get_config(config)) {
        return 0; // Default to 0 on failure
    }
    return (config >> ADCRANGE_NBIT) & 1;
}


/**************************************************************************/
/*!
    @brief Sets the shunt calibration by resistor for INA228.
    @param shunt_res Resistance of the shunt in ohms (floating point)
    @param max_current Maximum expected current in A (floating point)
*/
/**************************************************************************/
void INA228::set_shunt(float shunt_res, float max_current) noexcept {
  this->shunt_ohms_ = shunt_res;
  current_lsb_ = max_current / CURRENT_LSB_DIVISOR;
  update_shunt_cal_register();
}
float INA228::get_current_lsb() const noexcept {
    return current_lsb_;
}
void INA228::update_shunt_cal_register() {
  float scale = 1;
  if (get_adc_range()) {
    scale = 4;
  }
  float shunt_cal = SHUNT_CAL_COEFF * shunt_ohms_ * current_lsb_ * scale;
  // Round to nearest integer and clamp to valid 15-bit range
  uint16_t cal_value = (uint16_t)(shunt_cal + 0.5f);
  if (shunt_cal > static_cast<float>(SHUNT_CAL_MAX)) {
    cal_value = SHUNT_CAL_MAX;
    detail::DEBUG_PRINTF("Warning: shunt_cal overflow, clamped to %u\n", SHUNT_CAL_MAX);
  }
  detail::DEBUG_PRINTF("Shunt calibration value: %x\n", cal_value);
  (void)write_register16(INA228_Register::SHUNT_CAL, cal_value);
}
bool INA228::get_shunt_cal_register(uint16_t &cal_value) const {
    return read_register16(INA228_Register::SHUNT_CAL, cal_value);
}
bool INA228::shunt_calib() {
    this->set_shunt(this->shunt_ohms_, this->max_current_);
    return true;
}

bool INA228::shunt_tempco() {
    detail::DEBUG_PRINTF("Setting shunt temperature compensation value\n");
    return write_register16(INA228_Register::SHUNT_TEMPCO, SHUNT_TEMPCO_VALUE);
}

bool INA228::reset_all() {
    uint16_t config = 1 << RST_NBIT;
    detail::DEBUG_PRINTF("Resetting all (config=0x%04X)\n", config);
    return write_register16(INA228_Register::CONFIG, config);
}

bool INA228::reset_energy() {
    uint16_t config = 1 << RSTACC_NBIT;
    detail::DEBUG_PRINTF("Resetting energy/charge accumulation (config=0x%04X)\n", config);
    return write_register16(INA228_Register::CONFIG, config);
}

float INA228::get_energy() const {
    uint64_t raw = 0;
    if (!read_register40(INA228_Register::ENERGY, raw)) {
        detail::DEBUG_PRINTF("Failed to read energy register\n");
        return 0.0;
    }
    float energy = raw * ENERGY_COEFF * current_lsb_;
    detail::DEBUG_PRINTF("Energy raw=0x%llX, value=%f\n", raw, energy);
    return energy;
}

float INA228::get_power() const {
    uint32_t raw = 0;
    if (!read_register24(INA228_Register::POWER, raw)) {
        detail::DEBUG_PRINTF("Failed to read power register\n");
        return 0.0;
    }
    float power = POWER_COEFF * raw * current_lsb_;
    detail::DEBUG_PRINTF("Power raw=0x%06X, value=%f\n", raw, power);
    return power;
}

float INA228::get_temp() const {
    uint16_t raw = 0;
    if (!read_register16(INA228_Register::DIETEMP, raw)) {
        detail::DEBUG_PRINTF("Failed to read temperature register\n");
        return 0.0;
    }
    float temp = detail::varint2float(raw, 0, 16, DIETEMP_LSB);
    detail::DEBUG_PRINTF("Temperature raw=0x%04X, value=%f°C\n", raw, temp);
    return temp;
}

float INA228::get_vbus() const {
    uint32_t raw = 0;
    if (!read_register24(INA228_Register::VBUS, raw)) {
        detail::DEBUG_PRINTF("Failed to read VBUS register\n");
        return 0.0;
    }
    float vbus = detail::varint2float(raw, 4, 20, VBUS_LSB);
    detail::DEBUG_PRINTF("VBUS raw=0x%06X (shifted=0x%05X), value=%f V\n", raw, raw >> 4, vbus);
    return vbus;
}
float INA228::get_current() const {
    uint32_t raw = 0;
    if (!read_register24(INA228_Register::CURRENT, raw)) {
        detail::DEBUG_PRINTF("Failed to read current register\n");
        return 0.0;
    }
    float current = detail::varint2float(raw, 4, 20, current_lsb_);
    detail::DEBUG_PRINTF("Current raw=0x%06X (shifted=0x%05X), value=%f A\n", raw, raw >> 4, current);
    return current;
}

float INA228::get_charge() const {
    uint64_t raw = 0;
    if (!read_register40(INA228_Register::CHARGE, raw)) {
        detail::DEBUG_PRINTF("Failed to read charge register\n");
        return 0.0;
    }
    float charge = detail::varint2float(raw, 0, 40, current_lsb_);
    detail::DEBUG_PRINTF("Charge raw=0x%llX, value=%f\n", raw, charge);
    return charge;
}

void INA228::print_manufacturer_id() const {
    uint16_t raw_id = 0;
    if (!read_register16(INA228_Register::MANUFACTURER_ID, raw_id)) {
        detail::DEBUG_PRINTF("Failed to read manufacturer ID\n");
        return;
    }
    uint8_t first_byte = (raw_id >> 8) & 0xFF;
    uint8_t second_byte = raw_id & 0xFF;
    printf("Manufacturer ID (HEX): 0x%04X\n", raw_id);
    printf("Manufacturer ID (CHAR): %c%c\n", first_byte, second_byte);
}

void INA228::print_device_id() const {
    uint16_t raw_id = 0;
    if (!read_register16(INA228_Register::DEVICE_ID, raw_id)) {
        detail::DEBUG_PRINTF("Failed to read device ID\n");
        return;
    }
    uint16_t device_id = (raw_id >> 4) & 0xFFF;
    uint8_t revision = raw_id & 0x0F;
    printf("Device ID: 0x%03X\n", device_id);
    printf("Revision: 0x%X\n", revision);
}

float INA228::get_shunt_conv_factor() const noexcept {
    if (this->get_adc_range() == 0) {
        return VSHUNT_LSB_163MV * CURRENT_LSB_DIVISOR;  // 163.84mV range
    } else {
        return VSHUNT_LSB_40MV * CURRENT_LSB_DIVISOR;   // 40.96mV range
    }
}

void INA228::get_diag_alerts(INA228_Alert alert) const {
    uint16_t raw = 0;
    if (!read_register16(INA228_Register::DIAG_ALRT, raw)) {
        printf("Failed to read DIAG_ALRT register\n");
        return;
    }
    
    switch(alert) {
        case INA228_Alert::MEMSTAT:
            if ((raw & 0x0001) == 0x0000) {
                printf("MEMSTAT: Checksum error is detected in the device trim memory space\n");
            }
            break;
        case INA228_Alert::CNVRF:
            if ((raw & 0x0002) == 0x0002) {
                printf("CNVRF: Conversion is completed\n");
            }
            break;
        case INA228_Alert::BUSUL:
            if ((raw & 0x0004) == 0x0004) {
                printf("BUSUL: Bus voltage measurement falls below the threshold limit\n");
            }
            break;
        case INA228_Alert::BUSOL:
            if ((raw & 0x0008) == 0x0008) {
                printf("BUSOL: Bus voltage measurement exceeds the threshold limit\n");
            }
            break;
        case INA228_Alert::SHNTUL:
            if ((raw & 0x0010) == 0x0010) {
                printf("SHNTUL: Shunt voltage measurement falls below the threshold limit\n");
            }
            break;
        case INA228_Alert::SHNTOL:
            if ((raw & 0x0040) == 0x0040) {
                printf("SHNTOL: Shunt voltage measurement exceeds the threshold limit\n");
            }
            break;
        case INA228_Alert::TMPOL:
            if ((raw & 0x0080) == 0x0080) {
                printf("TMPOL: Temperature measurement exceeds the threshold limit\n");
            }
            break;
        case INA228_Alert::MATHOF:
            if ((raw & 0x0100) == 0x0100) {
                printf("MATHOF: Arithmetic operation resulted in an overflow error\n");
            }
            break;
        case INA228_Alert::CHARGEOF:
            if ((raw & 0x0200) == 0x0200) {
                printf("CHARGEOF: 40 bit CHARGE register has overflowed\n");
            }
            break;
        case INA228_Alert::ENERGYOF:
            if ((raw & 0x0400) == 0x0400) {
                printf("ENERGYOF: 40 bit ENERGY register has overflowed\n");
            }
            break;
        case INA228_Alert::APOL:
            if ((raw & 0x0800) == 0x0800) {
                printf("APOL: Alert pin polarity inverted (active-high, open-drain)\n");
            } else {
                printf("APOL: Alert pin polarity normal (active-low, open-drain)\n");
            }
            break;
        case INA228_Alert::SLOWALERT:
            if ((raw & 0x2000) == 0x2000) {
                printf("SLOWALERT: ALERT comparison on averaged value\n");
            } else {
                printf("SLOWALERT: ALERT comparison on non-averaged (ADC) value\n");
            }
            break;
        case INA228_Alert::CNVR:
            if ((raw & 0x4000) == 0x4000) {
                printf("CNVR: Conversion ready flag enabled on ALERT pin\n");
            } else {
                printf("CNVR: Conversion ready flag disabled on ALERT pin\n");
            }
            break;
        case INA228_Alert::ALATCH:
            if ((raw & 0x8000) == 0x8000) {
                printf("ALATCH: Latched\n");
            } else {
                printf("ALATCH: Transparent\n");
            }
            break;
        default:
            break;
    }
}

bool INA228::set_shunt_overvoltage(float value) {
    uint16_t data;
    float conv_factor = get_shunt_conv_factor();
    
    if (value >= 0) {
        data = (uint16_t)((value * this->shunt_ohms_) / conv_factor);
    } else {
        float value_temp = value * (-1.0);
        int16_t temp = (int16_t)((value_temp * this->shunt_ohms_) / conv_factor);
        data = (uint16_t)(~temp + 1);
    }
    
    detail::DEBUG_PRINTF("Setting shunt overvoltage threshold: %f (data=0x%04X)\n", value, data);
    return write_register16(INA228_Register::SOVL, data);
}

bool INA228::set_shunt_undervoltage(float value) {
    uint16_t data;
    float conv_factor = get_shunt_conv_factor();
    
    if (value >= 0) {
        data = (uint16_t)((value * this->shunt_ohms_) / conv_factor);
    } else {
        float value_temp = value * (-1.0);
        int16_t temp = (int16_t)((value_temp * this->shunt_ohms_) / conv_factor);
        data = (uint16_t)(~temp + 1);
    }
    
    detail::DEBUG_PRINTF("Setting shunt undervoltage threshold: %f (data=0x%04X)\n", value, data);
    return write_register16(INA228_Register::SUVL, data);
}

bool INA228::set_bus_overvoltage(float value) {
    uint16_t data = (uint16_t)(value / (16.0f * VBUS_LSB));
    detail::DEBUG_PRINTF("Setting bus overvoltage threshold: %f V (data=0x%04X)\n", value, data);
    return write_register16(INA228_Register::BOVL, data);
}

bool INA228::set_bus_undervoltage(float value) {
    uint16_t data = (uint16_t)(value / (16.0f * VBUS_LSB));
    detail::DEBUG_PRINTF("Setting bus undervoltage threshold: %f V (data=0x%04X)\n", value, data);
    return write_register16(INA228_Register::BUVL, data);
}

bool INA228::set_temp_limit(float value) {
    uint16_t data = (uint16_t)(value * INVERSE_TEMP_LSB);
    detail::DEBUG_PRINTF("Setting temperature limit: %f (data=0x%04X)\n", value, data);
    return write_register16(INA228_Register::TEMP_LIMIT, data);
}

bool INA228::set_power_overlimit(float value) {
    uint16_t data = (uint16_t)(value / (256 * 3.2 * current_lsb_));
    detail::DEBUG_PRINTF("Setting power overlimit: %f (data=0x%04X)\n", value, data);
    return write_register16(INA228_Register::PWR_LIMIT, data);
}

void INA228::configure() {
    (void)set_config();
    sleep_ms(10);
    (void)set_adc_config();
    sleep_ms(10);
    (void)shunt_calib();
    sleep_ms(10);
    (void)shunt_tempco();
    sleep_ms(10);
    (void)reset_energy();
    sleep_ms(10);
}
