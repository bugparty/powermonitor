# hardware info

## parts
[Adafruit Feather RP2040 x1](https://learn.adafruit.com/adafruit-feather-rp2040-pico)

[adafruit ina228](https://learn.adafruit.com/adafruit-ina228-i2c-power-monitor)

## prototype

# setup guide

## prepare
```
git clone -b master https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
```
Set PICO_SDK_PATH to this folder in your environment.
## if you are using windows
install this  zadig driver in order to flash
https://zadig.akeo.ie/
## build
```
mkdir build
cd build
cmake ..
make
```
## flash
Flash to Pico

Hold BOOTSEL while plugging in the Pico → mounts as RPI-RP2.

Drag & drop the generated .uf2 file.

[ref](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf)

## usb throughput stress mode

Firmware now supports a USB stress-stream mode using the same `DATA_SAMPLE` protocol frame.

- Enable by setting bit15 in `STREAM_START.channel_mask`:
  - `channel_mask = original_mask | 0x8000`
- In stress mode, device sends fixed-value samples as fast as the main loop can sustain:
  - `vbus_raw = 0x0F000`
  - `vshunt_raw = 0x00100`
  - `current_raw = 0x01000`
  - `dietemp_raw = 4480`
- Burst sending per main-loop iteration is limited by firmware constant:
  - `kStressBurstFramesPerLoop` in `device/powermonitor.cpp`
- `period_us` is ignored in stress mode.
- Normal mode behavior remains unchanged when bit15 is not set.
