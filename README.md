# Battery Monitoring System
Design for a Battery Monitoring System, to be used for a 4-cell LiFePO4 battery on a sailing boat. 

Highlights are:
* Chargers are currently Victron devices (shore power, solar, engine through DC/DC) that have LiFePO4 profiles, but are unaware of each other's presence, the SOC or the cumulative current flowing into the cells. Rather than trying to make them work in unison, idea behind this design is to take control over the process, to enforce proper cycles and prevent memory effects, and, of course, over and under charging.
* Solid State Relays to switch charger and/or load, depending on cell voltage, SOC and tail current.
* Wemos D1 Mini sends [data to SignalK](https://github.com/marcobergman/ESP8266SignalkClient) through WIFI.
* Current measuring to calculate SOC and tail current.
* Temperature measuring to prevent charging onder zero celcius, overheating, and to be able to respond to temperature drift of measurements.
* No active or passive cell balancing. That's a later phase. Let's first see if that is needed.

![image](https://github.com/marcobergman/bms/assets/17980560/288c4c5a-cf40-4fb4-bc63-0ceb95b42163)

Questions:
* Measuring current. Since ADS1115 can only measure positive voltage drop over shunt resistor, question arises how to measure negative current. Other option is to elevate the voltage drop with a stable reference voltage, but that is still prone to drift. Solution is the two anti-parallel differential amplifiers feeding into two different ADC input. Any more bright idea's?
* Contrary to drawings, SSR relays only have Normal Open 'contacts'. Idea is that if Wemos fouls up, you can pull it from its socket and then the relays will be 'on'.
