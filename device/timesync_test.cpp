// slave.cpp
#include "time_sync.hpp"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include <cstring>
#include <cstdio>
#include <cstdint>  // added for int64_t

// global Unix-epoch offset (microseconds)
static int64_t g_epoch_offset_us = 0;

// returns current Unix time in microseconds
static uint64_t get_unix_time_us() {
    uint64_t mono = to_us_since_boot(get_absolute_time());
    return mono + g_epoch_offset_us;
}

#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1
// Wait for USB CDC connection (host enumeration)
static void wait_usb_connected() {
    while (!stdio_usb_connected()) {
        sleep_ms(10);
    }
    printf("SYNC-SLAVE READY\n");
    fflush(stdout);
}

int main() {
    stdio_init_all();
    wait_usb_connected();

    const size_t REQ_SIZE = sizeof(TimeSyncRequest);
    TimeSyncRequest req;

    while (true) {
        // Read request packet
        size_t received = 0;
        uint8_t* ptr = reinterpret_cast<uint8_t*>(&req);
        while (received < REQ_SIZE) {
            int c = getchar_timeout_us(500000);
            if (c == PICO_ERROR_TIMEOUT) {
                // Timeout, retry
                received = 0;
                continue;
            }
            //if (c < 0) continue;
            //printf("received: %d\n", c);

            ptr[received++] = static_cast<uint8_t>(c);
        }
        // printf("DEBUG: recv %zu bytes\n", received);
        fflush(stdout);
        // Validate request packet header and CRC
        if (req.header != 0xAA55) {
            //printf("DEBUG: bad header 0x%04X\n", req.header);
            fflush(stdout);
            continue;
        }

        // Validate CRC
        uint16_t crc_calc = TimeSync::calc_crc(reinterpret_cast<uint8_t*>(&req), REQ_SIZE - sizeof(req.crc));
        if (crc_calc != req.crc) {
            continue;
        }

        if (req.seq == 0) {
            // Master sets Unix time (microseconds since epoch)
            uint64_t mono = to_us_since_boot(get_absolute_time());
            g_epoch_offset_us = (int64_t)req.T1 - (int64_t)mono;
            printf("DEBUG: Set epoch_offset_us = %lld\n", g_epoch_offset_us);
            fflush(stdout);
            continue; // do not send normal reply
        }

        // Construct reply packet
        TimeSyncReply rep;
        rep.header = 0xAA55;
        rep.seq    = req.seq;
        rep.T1     = req.T1;
        rep.T2     = get_unix_time_us();
        rep.T3     = get_unix_time_us();
        // Calculate reply CRC (overwrites previous fields)
        rep.crc    = TimeSync::calc_crc(reinterpret_cast<uint8_t*>(&rep), sizeof(rep) - sizeof(rep.crc));

        // Send reply
        fwrite(&rep, 1, sizeof(rep), stdout);
        fflush(stdout);

        const size_t RESP_CMD_SIZE = sizeof(TimeSyncCmd);
        TimeSyncCmd resp_cmd;
        received = 0;
        ptr = reinterpret_cast<uint8_t*>(&resp_cmd);
        while (received < RESP_CMD_SIZE) {
            int c = getchar_timeout_us(500000);
            if (c == PICO_ERROR_TIMEOUT) {
                // Timeout, retry
                received = 0;
                continue;
            }
            ptr[received++] = static_cast<uint8_t>(c);
        }
        if (resp_cmd.header != 0xAA56) {
            //printf("DEBUG: bad header 0x%04X\n", req.header);
            continue;
        }
        crc_calc = TimeSync::calc_crc(reinterpret_cast<uint8_t*>(&resp_cmd), RESP_CMD_SIZE - sizeof(resp_cmd.crc));
        if (crc_calc != resp_cmd.crc) {
            continue;
        }
        g_epoch_offset_us += resp_cmd.o;
    }
}
