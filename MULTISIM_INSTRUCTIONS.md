Multisim build instructions and component details

See README.md for quick start. Below is a step-by-step component placement and wiring checklist you can follow to recreate the Multisim project manually.

1) Create new Multisim project (mixed signal)
2) Place components:
   - Arduino UNO (or ATmega328 module)
   - Pulse Voltage Source (Function Generator) -> set square wave 0..5V
   - 4x 7-segment (single-digit) or a 4-digit 7-seg component
   - 8x 220Ω resistors for segments
   - 4x 2N3904 NPN transistors for digit drive
   - 5x SPST switches for MODE/UP/DOWN/START/RESET
   - Optional Schmitt inverter 74HC14 for pulse clean-up
   - Power rails +5V and GND

3) Wiring summary:
   - Connect Arduino Vcc to +5V and GND
   - Connect pulse source out to Arduino D2 (INT0). Add series 1k and 100nF to ground if you want filtering.
   - Connect Arduino digital pins D3..D9 to segment resistors then to segment pins A..G of each 7-seg.
   - Connect Arduino D10 to DP (if used).
   - Connect Digit transistors collectors to common cathodes/anodes of the 7-seg digits, bases via 4.7k to Arduino D11..D13,A0
   - Connect buttons between their pins and GND (using INPUT_PULLUP in code)

4) Test flow:
   - Upload compiled HEX into Arduino if VSM supports it.
   - Start simulation; vary pulse frequency to simulate speed; press START to begin fare counting.

Notes:
 - To speed up waiting-time testing, temporarily change WAIT_PERIOD_MS in code to 10 seconds.
 - If you want, I can generate and upload a .ms14 project file in a release; reply "add .ms14".
