# Battery Monitoring System
Design for a low-key, minimalistic Battery Monitoring System, to be used for a 4-cell LiFePO4 battery on a sailing boat. Well-known, well-documented and readily available parts, less is more.

Highlights are:
* Chargers are currently Victron devices (shore power, solar, engine through DC/DC) that have LiFePO4 profiles, but are unaware of each other's presence, the SOC or the cumulative current flowing into the cells. Rather than trying to make them work in unison, idea behind this design is to take control over the process, to enforce proper 20%-90% cycles to prevent memory effects, and, of course, prevent over and under charging.
* Solid State Relays to switch charger and/or load, depending on cell voltage, SOC and tail current.
* Wemos D1 Mini sends [data to SignalK](https://github.com/marcobergman/ESP8266SignalkClient) through WIFI.
* Current measuring to calculate SOC and tail current.
* Temperature measuring to prevent charging onder zero celcius, overheating, and to be able to respond to temperature drift of measurements.
* No active or passive cell balancing. That's a later phase. Let's first see if that is needed. I ordered grade A cells and like to see first what imbalance I get, and what currents I would need to balance them. I am not planning to charge my cells to that point where they are almost full up - and it seems to be only that part of the charge curve where you are able to effectively balance. Balancing on the flat part of the curve is very doubtful, and if at all would depend on the accuracy of the voltage measurements I'm able get with my intended setup. In the meantime, I'll bleed some charge of manually with a car headlight if needed.
* Contrary to drawings, [SSR relays](https://nl.aliexpress.com/item/32262347720.html) only have Normal Open 'contacts'. Idea is that if Wemos fouls up, you can pull it from its socket and then the relays will be 'Closed'.

![image](https://github.com/marcobergman/bms/assets/17980560/17fee2ed-b95e-4b68-945b-694634d19762)
* As for current sensing, I'll use an INA228. The resolution of the INA226 would suffice as well, but the INA226 does not accumulate power (SOC) so I'd have to keep the Wemos running. With the INA228, I can put the Wemos to sleep if it senses I shut down Signalk, and the alert output of the INA228 can then wake up the Wemos when the current goes over a certain limit.

Questions:

* BMS parameters can be set in the Wemos EEPROM. Current idea is to set those values in SignalK paths and let the Wemos compare it to its stored values, and replace those stored values if they are changed. Is there a better way of doing this?
