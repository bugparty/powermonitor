# PIO DMA I2C: Parsing and Value Corruption Post-Mortem

When transitioning from CPU-polled hardware I2C to fully asynchronous PIO DMA I2C, our read speeds dramatically improved. However, getting the PIO to DMA the memory *fast* was only half the battle. We spent considerable time debugging why the numbers parsed from memory were completely wrong, shifted, or full of garbage (`0xFFFF` or `0x0000`).

This document summarizes the classic "Data Integrity and Parsing" traps we hit when building the PIO DMA pipeline, and how we solved them.

---

## 1. The "Address Echo" Trap (Discarding TX Bytes)
**Symptom:** The values returned by the sensor looked suspiciously like device addresses (`0x80`, `0x81`) and register IDs (`0x05`, `0x04`) rather than electrical measurements.

**Root Cause:**
*   The `i2c.pio` bitbanger state machine operates identically for both sending and receiving. It executes a block of 8 clock cycles, clocks data on the SDA line, reads the ACK/NAK, and then **always pushes the 8-bit result of the bus to the RX FIFO**.
*   This means that when the DMA TX channel pushes the `Address(Write)`, `Register ID`, and `Address(Read)` bytes to the bus, the PIO clocks them out, samples the bus, and pushes those exact same bytes back into the RX FIFO!
*   Our original naive parser simply read the first 3 bytes from the RX DMA buffer expecting them to be the `VBUS` data. Instead, it was reading the I2C transaction header!

**Fix:** We mapped out the exact byte-by-byte footprint of a Repeated-Start read transaction. A standard register read sequence produces **3 echo bytes** followed by `N` data bytes.
Our parsing logic (`dma_rx_buf` indices) was rigidly updated to skip the 3 echo bytes:
*   `buf[0]`: `ADDR + WRITE` echo (Discard)
*   `buf[1]`: `Register ID` echo (Discard)
*   `buf[2]`: `ADDR + READ` echo (Discard)
*   `buf[3..5]`: Actual Data Bytes (Keep)

---

## 2. The Missing NAK Collision (Shifting/Garbage Bits)
**Symptom:** The first register (e.g., `VBUS`) would read correctly, but the second register (`VSHUNT`) would read as all `1`s (`0xFFFFFF`), or the bits would be shifted/corrupted.

**Root Cause:**
*   Standard I2C protocol requires the Master to send an **ACK (LOW)** after receiving intermediate bytes, but to send a **NAK (HIGH-Z)** after receiving the *final byte* of a sequence. This signals the Slave (INA228) to let go of the SDA line.
*   Initially, our DMA TX command generator pushed the same `cmd_read_8` (`NAK=0`) instruction for all 3 bytes.
*   Upon seeing an ACK on the 3rd byte, the INA228 assumed the Master wanted a 4th byte and continued to actively pull the SDA line LOW.
*   The Master then abruptly executed a `REP_START` or `STOP` instruction while the Slave was still fighting the bus. This collision corrupted the bus, breaking the subsequent transaction entirely.

**Fix:** In our `pio_i2c_build_command_sequence` logic, we added a special condition for the final byte of any register read loop. The PIO instruction's lowest bit controls the ACK/NAK pin drive. We explicitly set the `NAK` bit to `1` for the last byte read. This safely commands the INA228 to release the bus before the `REP_START` is triggered.

---

## 3. Endianness and the 32-bit RX FIFO Trap
**Symptom:** Bits were scrambled. High bytes became low bytes, and values seemed multiplied or completely out of bound.

**Root Cause:**
*   The PIO shifts the `SDA` pin data into the Input Shift Register (ISR) using `shift_right = false` (which means it shifts MSB-first, leftward, correctly matching standard I2C).
*   However, when the 8 bits are pushed to the RX FIFO, they form a 32-bit word where the 8 bits reside at the very bottom (LSB).
*   We originally attempted to use `DMA_SIZE_8` to densely pack these scattered 8-bit values into a continuous `uint8_t` memory array. But because the RP2040 DMA increments memory addresses differently based on source sizing, misaligned reads occurred, pulling 0s or wrong bytes from the 32-bit FIFO registers.

**Fix:** We embraced the 32-bit width of the RX FIFO.
*   We configured the RX DMA channel to use `DMA_SIZE_32` and transfer data into a `uint32_t dma_rx_buf[]` array.
*   While this wastes a slight amount of memory (32 bits used to store 8 bits of data per word), it guarantees perfect, hardware-aligned extraction.
*   During the parse phase on the CPU, we simply downcast the bottom 8 bits `(uint8_t)dma_rx_buf[offset]` and bit-shift them back together `(buf[0] << 16) | (buf[1] << 8) | buf[2]`. This completely bypassed any Little-Endian memory misalignment traps.
