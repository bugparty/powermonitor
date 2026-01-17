#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <inttypes.h>
#include "tusb.h"
/* ========================================================================
   INA228 Address Configuration
   ======================================================================== */
#define I2C_PORT i2c1
#define I2C_SDA 2 //adafruit feature I2C_SDA GPIO2
#define I2C_SCL  3 //adafruit feature I2C_SCL GPIO3
#define INA228_ADDR 0x40 // Default I2C address for INA228
#define INA228_SHUNT_OHMS 0.015

#include "INA228.hpp"
void debug(){
    int16_t cal_value = 0;
        // if (ina228.get_shunt_cal_register((uint16_t&)cal_value)) {
        //     printf("Shunt Calibration Register: %d hex %04X\n", cal_value, cal_value);
        // } else {
        //     printf("Failed to read Shunt Calibration Register\n");
        // }
}
void usb_cdc_try_write(const uint8_t *buf, uint32_t len) {
    if (!tud_cdc_connected()) return;
    if (!tud_cdc_write_available()) return;

    uint32_t n = tud_cdc_write(buf, len);
    tud_cdc_write_flush();
}

int main() {
    stdio_init_all();
    i2c_init(I2C_PORT, 400 * 1000);   // use  400khz rate
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
    uint32_t current_raw = 0;
    bool succeed = false;
    float buf[4];
    while (true) {
        uint64_t t0 = time_us_64();
        for(int i=0;i<1000;i++){
            buf[0] = ina228.get_vbus();
            buf[1] = ina228.get_current();
            buf[2] = ina228.get_power();
            buf[3] = ina228.get_energy();
            usb_cdc_try_write((uint8_t*)&buf, sizeof(buf));
        } 
        uint64_t t1 = time_us_64();
        printf("\nVBUS: %f V, Current: %f A, Power: %f W, Energy: %f J\n", buf[0], buf[1], buf[2], buf[3]);
        for(int i=0;i<10;i++){
            
            printf("elapsed = %llu us\n", t1 - t0);
            printf("average = %f us\n", (float)(t1 - t0)/1000);
        }
    }
}
