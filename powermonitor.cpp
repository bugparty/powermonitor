#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <inttypes.h>
#include "INA228.hpp"

#define I2C_PORT i2c1
#define I2C_SDA 2 //adafruit feature I2C_SDA GPIO2
#define I2C_SCL  3 //adafruit feature I2C_SCL GPIO3
#define INA228_ADDR 0x40 // Default I2C address for INA228

int main() {
    stdio_init_all();
    i2c_init(I2C_PORT, 100 * 1000);   // use default 100khz rate
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    sleep_ms(1500);

    INA228 ina228(I2C_PORT, INA228_ADDR, 1000); // 1k ohm shunt

    for(int i=8;i>0;i--){
        printf("Starting in %d...\n",i);
        sleep_ms(1000);
    }
    ina228.configure();
    sleep_ms(20);
    ina228.print_manufacturer_id();
    ina228.print_device_id();
    while (1) {
        ina228.get_vbus();
        ina228.get_current();
        ina228.get_power();
        ina228.get_energy();
        ina228.get_temp();
        ina228.get_charge();
        sleep_ms(1000);
    }
}
