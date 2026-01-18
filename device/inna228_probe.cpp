#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/binary_info.h"
// I2C defines
// This example will use I2C0 on GPIO8 (SDA) and GPIO9 (SCL) running at 400KHz.
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define I2C_PORT i2c0
#define I2C_SDA 8
#define I2C_SCL 9
#define I2C_BAUDRATE 100*1000
int64_t alarm_callback(alarm_id_t id, void *user_data) {
    // Put your timeout handler code in here
    return 0;
}


// I2C reserves some addresses for special purposes. We exclude these from the scan.
// These are any addresses of the form 000 0xxx or 111 1xxx
bool reserved_addr(uint8_t addr) {
    return (addr & 0x78) == 0 || (addr & 0x78) == 0x78;
}
void i2c_soft_reset(i2c_inst_t *i2c, uint baud) {
    i2c_deinit(i2c);
    sleep_ms(1);
    i2c_init(i2c, baud);   // 100kHz 起步更稳
}

int safe_i2c_read(i2c_inst_t *i2c, uint8_t addr, uint8_t *buf, size_t len, bool nostop, uint32_t timeout_us) {
    int rc = i2c_read_timeout_us(i2c, addr, buf, len, nostop, timeout_us);
    if (rc == PICO_ERROR_TIMEOUT) {
        // 读超时（比如对方一直时钟拉伸或总线卡住）
        return PICO_ERROR_TIMEOUT;
    } else if (rc == PICO_ERROR_GENERIC) {
        // NACK（例如地址不存在/设备没应答）
        return PICO_ERROR_GENERIC;
    }
    // 正常：返回读取的字节数
    return rc;
}
void i2c_bus_scan(){
     printf("\nI2C Bus Scan\n");
    printf("   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

    for (int addr = 0; addr < (1 << 7); ++addr) {
        if (addr % 16 == 0) {
            printf("%02x ", addr);
        }

        // Perform a 1-byte dummy read from the probe address. If a slave
        // acknowledges this address, the function returns the number of bytes
        // transferred. If the address byte is ignored, the function returns
        // -1.

        // Skip over any reserved addresses.
        int ret;
        uint8_t rxdata;
        if (reserved_addr(addr))
            ret = PICO_ERROR_GENERIC;
        else
            ret = safe_i2c_read(i2c_default, addr, &rxdata, 1, false, 20000);

        // Show this address as available if the read was acknowledged.
        if (ret >= 0)
            printf("@");
        else if (ret == PICO_ERROR_GENERIC)
            printf(".");
        else if (ret == PICO_ERROR_TIMEOUT){
            // 读超时（比如对方一直时钟拉伸或总线卡住）
            // 这种情况通常是总线有问题，复位I2C总线
            i2c_soft_reset(i2c_default, 400*1000);
            printf("T");
        }
            
        else 
            printf("?");
        printf(addr % 16 == 15 ? "\n" : "  ");
    }
}
#define INA228_ADDR 0x40
bool ina228_present() {
    // 这里我们发一个空的数据包到设备地址，若能收到 ACK 就说明它在线
    return i2c_write_blocking(I2C_PORT, INA228_ADDR, NULL, 0, false) >= 0;
}
int main()
{
    stdio_init_all();

    // I2C Initialisation. Using it at 400Khz.
    i2c_init(I2C_PORT, 400*1000);
    
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
        // Make the I2C pins available to picotool
    bi_decl(bi_2pins_with_func(I2C_SDA, I2C_SCL, GPIO_FUNC_I2C));

    // For more examples of I2C use see https://github.com/raspberrypi/pico-examples/tree/master/i2c

    // Timer example code - This example fires off the callback after 2000ms
    add_alarm_in_ms(2000, alarm_callback, NULL, false);
    // For more examples of timer use see https://github.com/raspberrypi/pico-examples/tree/master/timer

    printf("System Clock Frequency is %d Hz\n", clock_get_hz(clk_sys));
    printf("USB Clock Frequency is %d Hz\n", clock_get_hz(clk_usb));
    // For more examples of clocks use see https://github.com/raspberrypi/pico-examples/tree/master/clocks

    for(int i=15;i>0;i--){
        printf("Starting in %d...\n",i);
        sleep_ms(1000);
    }
    while (true) {
        if (ina228_present()) {
            printf("INA228 found at address 0x%02x\n", INA228_ADDR);
        } else {
            printf("INA228 not found at address 0x%02x\n", INA228_ADDR);
        }
        i2c_bus_scan();
        
    }
}
