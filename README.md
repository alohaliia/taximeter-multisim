# Taxi Meter — Multisim Project and Arduino Firmware

This repository contains materials to build and simulate a taxi automatic meter (计费器) in Multisim and to run the matching Arduino firmware on a real Arduino UNO.

Contents
- README.md (this file)
- taximeter_multisim.ino — Arduino sketch implementing: start fee, per-km billing (charged per full km), waiting-time billing (charged per full 10 minutes), settings saved in EEPROM, 4-digit 7-seg display driver (XX.YY format), max 99.99元.
- MULTISIM_INSTRUCTIONS.md — step-by-step instructions to build the Multisim schematic (component list, wiring, test tips). This is a textual Multisim project you can recreate quickly.
- SCHEMATIC_ASCII.txt — ASCII wiring summary/diagram for quick reference.

Notes
- If your Multisim version supports Arduino VSM, load the compiled HEX of the sketch into the Arduino module. If not, run the Arduino on a real board and use Multisim to simulate only the pulse source and front-end.
- I can add a native Multisim project file (.ms14/.dsn) on request. If you want I will generate, pack into a ZIP release and attach it here — reply "add .ms14".

Quick start
1. Open Multisim and create a new mixed-signal project.
2. Place an Arduino UNO (or ATmega328P module) and connect +5V and GND.
3. Place a square-wave voltage source (Pulse) and connect it to D2 (INT0) to simulate wheel pulses.
4. Place a 4-digit 7-seg display (or four single 7-seg), resistors and NPN transistors for digit multiplexing. Connect segments and digit-enable pins according to MULTISIM_INSTRUCTIONS.md.
5. Compile the sketch in Arduino IDE; if Multisim supports HEX upload, load the HEX into the Arduino module. Otherwise run on real Arduino and connect the same wiring used in Multisim.

If you want the ready-made Multisim .ms14 file, reply "add .ms14" and I'll put it in a release.
