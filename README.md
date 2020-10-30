# Capacitor Meter
Measures capacitance and displays value on a OLED Display using an ATTiny85 (Digistump)

## Features
- Based on ATTiny85 (DigiStump)
- SSD1306 0.96" OLED Display
- Frequency measurement for low value capacitors 1pF - 1uF using 555 oscillator
- Charge time measurement for high value capacitors 1uF - 50000uF
- 2 separate ports used for th methods to minimise stary capacitance
- Two values of current used for Charge Time to minimise time for large capacitors
- 555 method self zeros at start up, can be rezeroed with push button
- A quick test used to select which method should be used for each cycle of measurement.
- Charge time method accuracy can be improved by support for OSCVAL clock frequency adjustment

## Background

555 timer method uses a base frequency determined by a known capacitance (~ 1nF) (~15KHz)
Capacitance in parallel with this lowers the frequency
Frequency measured by pulse counting over a period of about 1 second and capacitance calculated.

Charge method first discharges the capacitor to 0V using a MOSFET. One of 2 currents is then selected via a MOSFETto start charging.
ADC is used to detect when 50% threshold is reached and capacitance calculated.

Measurement method is determined by using a quick charge methos to see if any capacitance is on the high value port and if so what current should be used.

 





