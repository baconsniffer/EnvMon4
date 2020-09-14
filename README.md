# EnvMon4
4th iteration of the in-house environment monitor

Modified to use UTC as the time base of the RTC with rules stored in EEPROM
to indicate local time and local daylight savings time to allow the time and
date to appear in the local time and to automatically switch between DST
and standard time. Also switched to using a DHT22 sensor to allow gathering
both humidity and temperature. The display was converted from a 2x16 LCD
using a parallel interface to a 20x4 display with an I2C interface - more
data, fewer pins.
