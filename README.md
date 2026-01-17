
# pc program
timesync.py
pip install pyserial

# pc protocol simulator (host-only)

This repository now includes a PC-side protocol simulator that runs entirely on the host
without Pico SDK dependencies. It includes a virtual serial link, protocol parser/builder,
device-side INA228 behavior simulation, and a demo event loop that exercises PING/SET_CFG/STREAM.

## build (host, no pico sdk)
```
g++ -std=c++17 -I. \
  app/main.cpp \
  protocol/crc16_ccitt_false.cpp \
  protocol/frame_builder.cpp \
  protocol/parser.cpp \
  protocol/unpack.cpp \
  sim/event_loop.cpp \
  sim/virtual_link.cpp \
  node/ina228_model.cpp \
  node/pc_node.cpp \
  node/device_node.cpp \
  -o pc_sim
```

## build (cmake, host)
```
cmake -S pc_sim -B build_pc
cmake --build build_pc --target pc_sim
```

## run
```
./pc_sim
```

## adjust link fault injection
Edit `app/main.cpp` to change `LinkConfig` fields such as `min_chunk`, `max_chunk`,
`min_delay_us`, `max_delay_us`, `drop_prob`, and `flip_prob`.

## adjust waveform parameters
Edit `node/ina228_model.cpp` to change default voltage/current/temperature waveforms.
