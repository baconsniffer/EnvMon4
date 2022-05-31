# EnvMon4
4th iteration of my little clock/environment monitor.

Modified to use UTC as the time base of the RTC with rules stored in EEPROM
to indicate local time and local daylight savings time offsets. The data is
always logged and displayed on the serial console in ISO8601 UTC format.
Time display on the LCD is jumper configurable to show local or UTC.
The default (no jumper installed) is to display local time on the LCD.
Added a DHT22 sensor to allow gathering both humidity and temperature. 

Compile time options allow for selecting temperature and humidity from
available sensors. Right now temperature can be read from the DHT22 or the
DS3231. (I've noticed that there is about a 2 C difference between the two
temperature sensors with the DS3231 reading higher than the DHT).
Humidity is only available from the DHT22. Other sensors are possible.

The display was converted an I2C interface to save pins and make physical
assembly more convenient. The 16x2 size was maintained but will have to be
enlarged if more sensor options are added.
