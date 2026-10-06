See releases for the latest release.
Scroll down for read me

<img width="1512" height="2016" alt="vu2" src="https://github.com/user-attachments/assets/05a6aa7d-aa94-42e6-8d3b-9c912795302b" />
This program is for the low cost 2.8" cheap yellow display board (CYD). The one I wrote for has the older micro-USB port. Newer boards may have different pin assignments so check your documentation. 
Power is from the USB connector. The USB port A on the rear of the radio may be used to power the display.

Written to read the analog voltages off the meter outputs of a Kenwood TS890 (and 990) amateur radio transceiver. It may work for other brands and models with some tweaking.
The CYD board only has one useable analog input, so a 2nd circuit board, the low cost  ADS1115 Amplifier Module 16 Bit Analog to Digital Development Board ADC Converter Module 4 Channel Development Board from 
Amazon is used. It connects using the small 4-wire connecting cable to the CYD for power and data. A mini stereo plug and cable is required to connect the A/D board to the meter jack on the radio for analog inputs
0 and 1.

The screen brightness and the meter types for the left and right meter modified using the touch screen. Touch top-left to dim, top-right to brighten the screen.
Touch the meter type on the bottom of the left and right meters to toggle through PWR, ALC, VOLTS, COMP, AMPS, SWR. and RAW input voltage (0-3 volts).
Meter types and brightness levels are stored in non-volatile memory.  Once the meter type is selected, the radio must be programmed to match. For the Kenwood 890:
Menu - ADV menu item 0 - Meter 1 (left VU meter) transmit meter value (and always S meter in recieve).
Menu - ADV menu item 1 = Meter 2 (right VU meter) transmit meter value
Menu - ADV menu item 2 and item 3 = Output level percent. Turn the RF Gain knob completely counter-clockwise and observe meter 1 display. Adjust the level for the S meter to indicate full scale.
Use this value for both menu item 2 and 3. By default, my radio is set to 80% for full scale deflection.

The program is free for PRIVATE USE ONLY and my not be used for any resale or commercial applications. SO there.
73s. WD5ACP


