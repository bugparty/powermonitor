#ifndef INA228_HPP
#define INA228_HPP

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <inttypes.h>

/* ========================================================================
   INA228 Debug Control
   ======================================================================== */
#define INA228_DEBUG 1
#if INA228_DEBUG
#define INA228_PRINTF(...) printf(__VA_ARGS__)
#else
#define INA228_PRINTF(...)
#endif

class INA228 {
public:
    /* ========================================================================
    INA228 Register Addresses
    ======================================================================== */
    enum class INA228_Register : uint8_t {
        CONFIG              = 0x00,
        ADC_CONFIG          = 0x01,
        SHUNT_CAL           = 0x02,
        SHUNT_TEMPCO        = 0x03,
        VSHUNT              = 0x04,
        VBUS                = 0x05,
        DIETEMP             = 0x06,
        CURRENT             = 0x07,
        POWER               = 0x08,
        ENERGY              = 0x09,
        CHARGE              = 0x0A,
        DIAG_ALRT           = 0x0B,
        SOVL                = 0x0C,
        SUVL                = 0x0D,
        BOVL                = 0x0E,
        BUVL                = 0x0F,
        TEMP_LIMIT          = 0x10,
        PWR_LIMIT           = 0x11,
        MANUFACTURER_ID     = 0x3E,
        DEVICE_ID           = 0x3F
    };

    /* ========================================================================
       INA228 ADC alerts signal
       ======================================================================== */
    enum class INA228_Alert : uint8_t {
      MEMSTAT     = 0,
      CNVRF       = 1,
      POL         = 2,
      BUSUL       = 3,
      BUSOL       = 4,
      SHNTUL      = 5,
      SHNTOL      = 6,
      TMPOL       = 7,
      MATHOF      = 9,
      CHARGEOF    = 10,
      ENERGYOF    = 11,
      APOL        = 12,
      SLOWALERT   = 13,
      CNVR        = 14,
      ALATCH      = 15
   };

    /* ========================================================================
       INA228 Shunt Temperature Coefficient
       ======================================================================== */
    static constexpr uint16_t SHUNT_TEMPCO_VALUE = 0;

    /* ========================================================================
       INA228 Reset bytes for CONFIG [15] / [14]
       Description: 
       - RST: Generates a system reset that is the same as power-on reset.
       - RSTACC: Resets the contents of accumulation registers ENERGY and CHARGE to 0
       ======================================================================== */
    static constexpr uint8_t RST_NBIT = 15;
    static constexpr uint8_t RSTACC_NBIT = 14;

    /* ========================================================================
       INA228 Initial conversion delay CONFIG [13:6]
       Description: Delay for initial ADC conversion in steps of 2 ms
       Values:
       - 0x00 - 0 seconds (default)
       - 0x01 - 2 ms
       - 0xFF - 510ms
       ======================================================================== */
    static constexpr uint8_t CONVERSION_DELAY = 0x00;
    static constexpr uint8_t CONVDLY_NBIT = 6;

    /* ========================================================================
       INA228 Temperature compensation CONFIG [5]
       Description: Enables temperature compensation of an external shunt
       Values:
       - 0x00 - disabled (default)
       - 0x01 - enabled
       ======================================================================== */
    static constexpr uint8_t TEMP_COMP = 0x00;
    static constexpr uint8_t TEMPCOMP_NBIT = 5;

    /* ========================================================================
       INA228 ADC Shunt full scale range selection CONFIG [4]
       Description: Shunt full scale range selection across IN+ and IN–.
       Values:
       - 0x00 - ±163.84 mV (default)
       - 0x01 - ±40.96 mV
       ======================================================================== */
    static constexpr uint8_t ADCRANGE = 0x00;
    static constexpr uint8_t ADCRANGE_NBIT = 4;

    /* ========================================================================
       INA228 ADC Mode
       Description: The user can set the MODE bits for continuous or triggered 
       mode on bus voltage, shunt voltage or temperature measurement.
       Values:
       - 0x00h = Shutdown
       - 0x01h = Triggered bus voltage, single shot
       - 0x02h = Triggered shunt voltage, single shot
       - 0x03h = Triggered shunt voltage and bus voltage, single shot
       - 0x04h = Triggered temperature, single shot
       - 0x05h = Triggered temperature and bus voltage, single shot
       - 0x06h = Triggered temperature and shunt voltage, single shot
       - 0x07h = Triggered bus voltage, shunt voltage and temperature, single shot
       - 0x08h = Shutdown
       - 0x09h = Continuous bus voltage only
       - 0x0Ah = Continuous shunt voltage only
       - 0x0Bh = Continuous shunt and bus voltage
       - 0x0Ch = Continuous temperature only
       - 0x0Dh = Continuous bus voltage and temperature
       - 0x0Eh = Continuous temperature and shunt voltage
       - 0x0Fh = Continuous bus voltage, shunt voltage and temperature (default)
       ======================================================================== */
    static constexpr uint8_t ADC_MODE = 0x0F;
    // ADC Mode ADC_CONFIG [15:12]
    static constexpr uint8_t ADC_MODE_NBIT = 12;

    /* ========================================================================
       INA228 ADC conversion time of bus voltage meas.
       Description: Sets the conversion time of the bus voltage measurement
       Values:
       - 0x00h = 50 μs
       - 0x01h = 84 μs
       - 0x02h = 150 μs
       - 0x03h = 280 μs
       - 0x04h = 540 μs
       - 0x05h = 1052 μs
       - 0x06h = 2074 μs
       - 0x07h = 4120 μs
       ======================================================================== */
    static constexpr uint8_t VBUS_CONV_TIME = 0x05;
    // ADC conversion time for bus voltage ADC_CONFIG [11:9]
    static constexpr uint8_t VBUS_CONV_TIME_NBIT = 9;

    /* ========================================================================
       INA228 ADC conversion time of shunt voltage meas.
       Description: Sets the conversion time of the shunt voltage measurement
       Values:
       - 0x00h = 50 μs
       - 0x01h = 84 μs
       - 0x02h = 150 μs
       - 0x03h = 280 μs
       - 0x04h = 540 μs
       - 0x05h = 1052 μs
       - 0x06h = 2074 μs
       - 0x07h = 4120 μs
       ======================================================================== */
    static constexpr uint8_t VSHCT_CONV_TIME = 0x05;
    // ADC conversion time for shunt voltage ADC_CONFIG [8:6]
    static constexpr uint8_t VSHCT_CONV_TIME_NBIT = 6;

    /* ========================================================================
       INA228 ADC conversion time of temperature meas.
       Description: Sets the conversion time of the temperature measurement
       Values:
       - 0x00h = 50 μs
       - 0x01h = 84 μs
       - 0x02h = 150 μs
       - 0x03h = 280 μs
       - 0x04h = 540 μs
       - 0x05h = 1052 μs
       - 0x06h = 2074 μs
       - 0x07h = 4120 μs
       ======================================================================== */
    static constexpr uint8_t VTCT_CONV_TIME = 0x05;
    // ADC conversion time for temperature ADC_CONFIG [5:3]
    static constexpr uint8_t VTCT_CONV_TIME_NBIT = 3;

    /* ========================================================================
       INA228 ADC sample averaging count. 
       Values: 
       - 0x00h = 1
       - 0x01h = 4
       - 0x02h = 16
       - 0x03h = 64
       - 0x04h = 128
       - 0x05h = 256
       - 0x06h = 512
       - 0x07h = 1024
       ======================================================================== */
    static constexpr uint8_t ADC_AVG = 0x00;
    // ADC sample averaging count ADC_CONFIG [2:0]
    static constexpr uint8_t AVG_NBIT = 0;

    i2c_inst_t *i2c;
    uint8_t addr;
    float shunt_ohms; ///< Shunt resistance value in ohms
    float _current_lsb; ///< Current LSB value used for calculations
    INA228(i2c_inst_t *i2c_inst, uint8_t i2c_addr, float shunt_ohms);
    
    bool i2c_read_reg_stop(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
    bool i2c_write_reg_stop(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
    bool i2c_read_reg_stop_timeout(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
    uint16_t to_bytes16(uint16_t register_value);
    uint8_t getADCMode();
    bool write_register16(INA228_Register reg, uint16_t register_value);
    bool read_register16(INA228_Register reg, uint16_t &register_value);
    bool read_register24(INA228_Register reg, uint32_t &register_value);
    bool read_register40(INA228_Register reg, uint64_t &register_value);
    
    bool set_config();
    bool get_config(uint16_t &config);
    bool set_adc_config();
    uint8_t get_adc_range();
    float get_current_lsb();
    bool shunt_calib();
    bool shunt_tempco();
    void _updateShuntCalRegister();
    bool get_shunt_cal_register(uint16_t &cal_value);
    void setShunt(float shunt_res, float max_current);
    bool reset_all();
    bool reset_energy();
    
    float convert2comp2float(int64_t twocompdata, uint8_t nrofbit, float factor);
    
    float get_energy();
    float get_power();
    float get_temp();
    float get_vbus();
    float get_current();
    float get_charge();
    
    void print_manufacturer_id();
    void print_device_id();
    
    float get_shunt_conv_factor();
    void get_diag_alerts(INA228_Alert alert);
    
    bool set_shunt_overvoltage(float value);
    bool set_shunt_undervoltage(float value);
    bool set_bus_overvoltage(float value);
    bool set_bus_undervoltage(float value);
    bool set_temp_limit(float value, float consta);
    bool set_power_overlimit(float value, float consta);
    
    void configure();
};

template<typename T> T  constexpr  get_mask(uint8_t nrofbit) {
   T mask = 0;
   for (uint8_t i = 0; i < nrofbit; i++) {
       mask |= (1 << i);
   }
   return mask;
}
template<typename T>  float varint2float(T twocompdata,const uint8_t shift,const uint8_t nrofbit,const float factor) {
   twocompdata = (twocompdata >> shift) & get_mask<T>(nrofbit);
   if (twocompdata & (1 << (nrofbit-1))) {
       twocompdata -= (1 << (nrofbit));
   }
   return (float)twocompdata * factor;
}


#endif // INA228_HPP
