#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <inttypes.h>
/* ========================================================================
   INA228 Address Configuration
   ======================================================================== */
#define I2C_PORT i2c1
#define I2C_SDA 2 //adafruit feature I2C_SDA GPIO2
#define I2C_SCL  3 //adafruit feature I2C_SCL GPIO3
#define INA228_ADDR 0x40 // Default I2C address for INA228
#define INA228_SHUNT_OHMS 0.015

#include "INA228.hpp"

int main() {
    stdio_init_all();
    i2c_init(I2C_PORT, 100 * 1000);   // use  400khz rate
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    sleep_ms(1500);

    INA228 ina228(I2C_PORT, INA228_ADDR, INA228_SHUNT_OHMS); // 1k ohm shunt

    for(int i=3;i>0;i--){
        printf("Starting in %d...\n",i);
        sleep_ms(1000);
    }
    ina228.configure();
    sleep_ms(20);
    ina228.print_manufacturer_id();
    ina228.print_device_id();
    while (1) {
        int16_t cal_value = 0;
        if (ina228.get_shunt_cal_register((uint16_t&)cal_value)) {
            printf("Shunt Calibration Register: %d hex %04X\n", cal_value, cal_value);
        } else {
            printf("Failed to read Shunt Calibration Register\n");
        }
        (void)ina228.shunt_calib();
        (void)ina228.get_vbus();
        (void)ina228.get_current();
        (void)ina228.get_power();
        (void)ina228.get_energy();
        (void)ina228.get_temp();
        (void)ina228.get_charge();
        sleep_ms(1000);
    }
}
