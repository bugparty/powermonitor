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

# pc program
timesync.py
pip install pyserial
