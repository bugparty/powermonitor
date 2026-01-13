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

/* ========================================================================
   INA228 Address Configuration
   ======================================================================== */
#define INA228_PORT 0
#define INA228_ADDRESS 0x00
#define INA228_SHUNT_OHMS 0.010
#define INA228_SHUNT_TEMPCO_VALUE 0

/* ========================================================================
   INA228 Reset bytes for CONFIG [15] / [14]
   Description: 
   - RST: Generates a system reset that is the same as power-on reset.
   - RSTACC: Resets the contents of accumulation registers ENERGY and CHARGE to 0
   ======================================================================== */
#define INA228_RST_NBIT 15
#define INA228_RSTACC_NBIT 14

/* ========================================================================
   INA228 Initial conversion delay CONFIG [13:6]
   Description: Delay for initial ADC conversion in steps of 2 ms
   Values:
   - 0x00 - 0 seconds (default)
   - 0x01 - 2 ms
   - 0xFF - 510ms
   ======================================================================== */
#define INA228_CONVERSION_DELAY 0x00
#define INA228_CONVDLY_NBIT 6

/* ========================================================================
   INA228 Temperature compensation CONFIG [5]
   Description: Enables temperature compensation of an external shunt
   Values:
   - 0x00 - disabled (default)
   - 0x01 - enabled
   ======================================================================== */
#define INA228_TEMP_COMP 0x00
#define INA228_TEMPCOMP_NBIT 5

/* ========================================================================
   INA228 ADC Shunt full scale range selection CONFIG [4]
   Description: Shunt full scale range selection across IN+ and IN–.
   Values:
   - 0x00 - ±163.84 mV (default)
   - 0x01 - ±40.96 mV
   ======================================================================== */
#define INA228_ADCRANGE 0x00
#define INA228_ADCRANGE_NBIT 4

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
#define INA228_ADC_MODE 0x0F
#define INA228_ADC_MODE_NBIT 12

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
#define INA228_VBUS_CONV_TIME 0x05
#define INA228_VBUS_CONV_TIME_NBIT 9

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
#define INA228_VSHCT_CONV_TIME 0x05
#define INA228_VSHCT_CONV_TIME_NBIT 6

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
#define INA228_VTCT_CONV_TIME 0x05
#define INA228_VTCT_CONV_TIME_NBIT 3

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
#define INA228_ADC_AVG 0x00
#define INA228_AVG_NBIT 0

/* ========================================================================
   INA228 ADC alerts signal
   ======================================================================== */
#define INA228_ALERT_MEMSTAT 0
#define INA228_ALERT_CNVRF 1
#define INA228_ALERT_POL 2
#define INA228_ALERT_BUSUL 3
#define INA228_ALERT_BUSOL 4
#define INA228_ALERT_SHNTUL 5
#define INA228_ALERT_SHNTOL 6
#define INA228_ALERT_TMPOL 7
#define INA228_ALERT_MATHOF 9
#define INA228_ALERT_CHARGEOF 10
#define INA228_ALERT_ENERGYOF 11
#define INA228_ALERT_APOL 12
#define INA228_ALERT_SLOWALERT 13
#define INA228_ALERT_CNVR 14
#define INA228_ALERT_ALATCH 15

/* ========================================================================
   INA228 Register Addresses
   ======================================================================== */
#define INA228_CONFIG              0x00
#define INA228_ADC_CONFIG          0x01
#define INA228_SHUNT_CAL           0x02
#define INA228_SHUNT_TEMPCO        0x03
#define INA228_VSHUNT              0x04
#define INA228_VBUS                0x05
#define INA228_DIETEMP             0x06
#define INA228_CURRENT             0x07
#define INA228_POWER               0x08
#define INA228_ENERGY              0x09
#define INA228_CHARGE              0x0A
#define INA228_DIAG_ALRT           0x0B
#define INA228_SOVL                0x0C
#define INA228_SUVL                0x0D
#define INA228_BOVL                0x0E
#define INA228_BUVL                0x0F
#define INA228_TEMP_LIMIT          0x10
#define INA228_PWR_LIMIT           0x11
#define INA228_MANUFACTURER_ID     0x3E
#define INA228_DEVICE_ID           0x3F

class INA228 {
public:
    i2c_inst_t *i2c;
    uint8_t addr;
    uint16_t shunt_ohms;
    
    INA228(i2c_inst_t *i2c_inst, uint8_t i2c_addr, uint16_t shunt_ohms);
    
    bool i2c_read_reg_stop(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
    bool i2c_write_reg_stop(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
    bool i2c_read_reg_stop_timeout(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
    uint16_t to_bytes16(uint16_t register_value);
    
    bool write_register16(uint8_t reg, uint16_t register_value);
    bool read_register16(uint8_t reg, uint16_t &register_value);
    bool read_register24(uint8_t reg, uint32_t &register_value);
    bool read_register40(uint8_t reg, uint64_t &register_value);
    
    bool set_config();
    bool set_adc_config();
    float get_current_lsb();
    bool shunt_calib();
    bool shunt_tempco();
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
    void get_diag_alerts(uint8_t alert);
    
    bool set_shunt_overvoltage(float value);
    bool set_shunt_undervoltage(float value);
    bool set_bus_overvoltage(float value);
    bool set_bus_undervoltage(float value);
    bool set_temp_limit(float value, float consta);
    bool set_power_overlimit(float value, float consta);
    
    void configure();
};

#endif // INA228_HPP
