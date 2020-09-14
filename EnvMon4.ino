/*
 * Simple time/temperature logger
 *
 * Uses a DS3231 RTC and LCD display.
 * Self-adjusts for Daylight Savings Time
 */
/****************************************************************

   Environmental monitor

   Track changes in temperature, barometric pressure and humidity
   and store the reading every two minutes to an SD card.
   Live data including date and time are displayed on a 20 x 4
   LCD module.
 ****************************************************************
   LCD interface based on:
   https://www.instructables.com/id/Using-PCF8574-backpacks-with-LCD-modules-and-Ardui/

   See also:
   https://docs.labs.mediatek.com/resource/linkit7697-arduino/en/tutorial/driving-1602-lcd-with-pcf8574-pcf8574a

   LCD Library: 
   https://bitbucket.org/fmalpartida/new-liquidcrystal/downloads

   Usage:
   https://bitbucket.org/fmalpartida/new-liquidcrystal/wiki/Home   
****************************************************************
   Hardware Setup

   Backpack     Arduino
   ========     =======
      SDA          A4
      SCL          A5

****************************************************************
    DHT22 Support
    https://github.com/adafruit/DHT-sensor-library
    
    Hardware Setup
    
    DHT         Arduino
    ===         =======
     1            Vcc
     2             8
     3            N/C
     4            Gnd
    
****************************************************************/

/*
 * Put includes here
 *
 */
#include <Wire.h>                 // I2C Communications
#include <EEPROM.h>               // Internal EEPROM for Timezone offsets
#include <LiquidCrystal_I2C.h>    // LCD backpack interface
#include <DHT.h>                  // DHT22 (AM2302) Temperature and Humidity Sensor
#include <DHT_U.h>
//
// For DS3231
#include <DS3232RTC.h>            // https://github.com/JChristensen/DS3232RTC
#include <Streaming.h>            // http://arduiniana.org/libraries/streaming/
#include <Timezone.h>             // https://github.com/JChristensen/Timezone
#include <DHT.h>                  // DHT22 (AM2302) Temperature and Humidity Sensor
#include <DHT_U.h>
/*
 * End of includes
 *
 */
 
/*
 * Put defines here
 *
 */
// for serial output debugging
#define DEBUG
#undef DEBUG
// For the LCD via I2C
#define SDA A4             // Serial data
#define SCL A5             // Serial clock
// For the EEPROM
#define EEBASE 100         // Base address
#define SIG1   0x55        // Expected signature value at EEBASE
#define SIG2   0xAA        // Expected signature value at EEBASE + 1
#define TZBASE EEBASE + 2  // Where Timezone rules are stored
// For the 12/24 hour mode
#define HR12 0
#define HR24 1
// For the DHT sensor
#define DHTPIN   8         // 1-wire interface to sensor
#define DHTTYPE  DHT22     // DHT22 (AM2302) sensor type
// LCD Interface
#define  LCDADDR 0x27      // LCD backpack i2c address in hex (PCF8574A is 0x3F
/*
 * End of defines
 *
 */
  
/*
 * Program constants
 *
 */
// define the custom bitmaps
// up to 8 bitmaps are supported
const uint8_t my_bitmap[][8] =
{
  {0x00, 0x0C, 0x12, 0x0C, 0x00, 0x00, 0x00, 0x00},  // degree symbol
  {0x04, 0x0E, 0x1F, 0x04, 0x04, 0x04, 0x00, 0x00},  // up arrow
  {0x00, 0x00, 0x04, 0x04, 0x04, 0x1F, 0x0E, 0x04},  // down arrow
  {0x00, 0x1F, 0x11, 0x11, 0x11, 0x11, 0x1F, 0x00},  // rectangle
  {0x1F, 0x1E, 0x1C, 0x1A, 0x11, 0x00, 0x00, 0x00},  // up-left arrow
  {0x1F, 0x0F, 0x07, 0x0B, 0x11, 0x00, 0x00, 0x00},  // up-right arrow
  {0x00, 0x00, 0x00, 0x11, 0x1A, 0x1C, 0x1E, 0x1F},  // down-left arrow
  {0x00, 0x00, 0x00, 0x11, 0x0B, 0x07, 0x0F, 0x1F},  // down-right arrow
};

// Useful names for days of the week and months
const char DoW[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char Mon[13][4] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
   
/*
 * End of constants
 *
 */
    
/*
 * Global objects here
 *
 */
// LCD display object
LiquidCrystal_I2C    lcd(LCDADDR, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);
LCD                  *myLCD = &lcd;                           // How we talk to the LCD

// CAN Eastern Time Zone (Toronto)
TimeChangeRule myDST = {"EDT", Second, Sun, Mar, 2, -240};    //Daylight time = UTC - 4 hours
TimeChangeRule mySTD = {"EST", First, Sun, Nov, 2, -300};     //Standard time = UTC - 5 hours
Timezone myTZ(myDST, mySTD);                                  // The default rules
// If TimeChangeRules are already stored in EEPROM they are read in setup().
TimeChangeRule *tcr;                                          //pointer to the time change rule, use to get TZ abbrev

// Temperature and humidity sensor
DHT                  dht(DHTPIN, DHTTYPE);                    // How we talk to the DHT22

/*
 * End of global objects
 *
 */
    
/*
 * Global simple variables here
*/

float         T, H;                                           // Temperature, Humidity
unsigned long delayTime = 0;                                  // Actual value set later
unsigned long lastm = millis();                               // Time since startup - do early
byte          t24 = HR24;                                     // time display format

/*
 * End of global simple variables
 *
 */
    
/*
 * Setup code
 *
 */

void setup () {
    Serial.begin(115200);
    Serial << endl << F("Blackfire Software") << endl << F("Startup ...") << endl;
    
    // Activate LCD module
    myLCD->begin(20, 4);           // 4 rows, 20 columns
    myLCD->backlight();            // Turn on backlight
    showIntro();                   // Display the splash screen

    // register the custom bitmaps
    int i;
    int bitmap_size = sizeof(my_bitmap) / sizeof(my_bitmap[0]);
  
    for (i = 0; i < bitmap_size; i++)
    {
       myLCD->createChar(i, (uint8_t *)my_bitmap[i]);
    }

    // Activate the DHT22 sensor
    dht.begin();
    Serial.println("DHT activated ...");

    // Un-comment to force update for testing
    // setClock();
    
    if (validSignature()) {    // Fetch rules from EEPROM
        Serial << F("EEPROM Valid");
        Timezone myTZ(EEBASE+2);
        Serial << F("Read Timezone Data") << endl;
    } else {                   // Go into timeset mode
        Serial << F("EEPROM Invalid") << endl << F("Setting time") << endl;
        setClock();
    }

    // setSyncProvider() causes the Time library to synchronize with the
    // external RTC by calling RTC.get() every five minutes by default.
    setSyncProvider(RTC.get);
	
	// 3600 s (1 h) between syncs with hardware clock
    setSyncInterval((time_t)3600);

    Serial << F("RTC Sync ");
	
    if (timeStatus() != timeSet) {    // Go into timeset mode
        Serial << F("RTC FAIL!");
        setClock();
    }
    Serial << endl;

    // RTC is up at this point and the display and sensors are ready
    // Attach RTC interrupt to pin 2 which connect to SQW on the RTC
    // SQW will toggle at 1 Hz and provide updates to the time and display
}

    
/*
 * Main program loop
 *
 */
void loop() {
   if (needDraw) {
      showReadings();
      // put code to check if time to log and set flag if so
      needDraw = 0;
   }

   if (needLog) {
      // log the data to the SD card
      // print the data to the serial monitor
      needLog = 0;
   }
/*
    static time_t tLast;
    time_t t;
    tmElements_t tm;
    int s;

    // Read UTC time
    time_t utc = now();
	
    // Convert to local
    t = myTZ.toLocal(utc, &tcr);
    
    if (t != tLast) {
        if (Serial.available())
           setClock();
           
        tLast = t;

        // 
        s = second(t);
        }
    }
*/
}

/*
 * Display the hours and minutes on the display
 * showNumberDec(int num, bool leading_zero, uint8_t length, uint8_t pos)
 *              (minute, leading 0, The number of digits to set, (0 - leftmost, 3 - rightmost))
 * showNumberDec(   m,      true,              2,                          2)
 */
void displayTime(time_t t) {
    // hours in hour(t)
    // minute in minute(t)
    
    uint8_t h = hour(t);    // internally, h is always in 24-hour format
    uint8_t m = minute(t);
    uint8_t s = second(t);
 
    if (t24 == 0)              // if showing 12-hour time
        h = hourFormat12(t);   // get the hour in 12-hour format

    // Show the 2 digit hour starting at position 0
    display.showNumberDecEx(h, dot, false, 2, 0);
    // Show the 2 digit minute with leading zero starting at position 2
    display.showNumberDec(m, true, 2, 2);
  
}

/*
 * Show the temperature of the DS3231 on-board sensor
 */
void displayTemp() {
    float c = RTC.temperature() / 4.;

    int t = (int)(c);

    data[0] = (t < 0 ? DASH : 0);           // Account for freezing
    data[1] = display.encodeDigit(t / 10);  // Tens
    data[2] = display.encodeDigit(t % 10);  // Units
    data[3] = DEGREE;                       // degree symbol

    display.setSegments(data);
}

/*
 * Show the 4 digit year on the display
 */
void displayYear(time_t t) {
    int yr = year(t);
#ifdef DEBUG
    Serial << F("Year: ") << yr << endl;
#endif
	/* use library call showNumberDec() to simplify - it does all the work
    data[0] = display.encodeDigit(yr / 1000);
    yr = (yr % 1000);
    data[1] = display.encodeDigit(yr / 100);
    yr = (yr % 100);
    data[2] = display.encodeDigit(yr / 10);
    yr = (yr % 10);
    data[3] = display.encodeDigit(yr);

    display.setSegments(data);
	*/
	display.showNumberDec(yr, false, 4, 0); // 4 digits starting at the leftmost position
}

/*
 * Show the month on the two middle digits of the display
 */
void displayMonth(time_t t) {
    int mo = month(t);
#ifdef DEBUG
    Serial << F("Month: ") << mo << endl;
#endif
    data[0] = DASH;
    data[1] = display.encodeDigit( mo / 10 );
    data[2] = display.encodeDigit( mo % 10 );
    data[3] = DASH;
  
    display.setSegments(data);
}

/*
 * Show the day of the month on the two middle digits of the display
 */
void displayDay(time_t t) {
    int dy = day(t);
#ifdef DEBUG
    Serial << F("Day: ") << dy << endl;
#endif
    data[0] = DASH;
    data[1] = display.encodeDigit( dy / 10 );
    data[2] = display.encodeDigit( dy % 10 );
    data[3] = BLANK;
  
    display.setSegments(data);
}

/*
 * The following routines are for the serial port
 */
 // print date and time to Serial
void printDateTime(time_t t)
{
    printDate(t);
    printTime(t);
	Serial << endl;
}

// print time to Serial
void printTime(time_t t)
{
    printI00(hour(t), ':');
    printI00(minute(t), ':');
    printI00(second(t), ' ');
}

// print date to Serial
void printDate(time_t t)
{
    Serial << _DEC(year(t)) << "-" << monthShortStr(month(t)) << "-";
    printI00(day(t), ' ');
}

// print temperature to serial
void printTemp() {
    float c = RTC.temperature() / 4.;
	
    Serial << F("Temperature: ") << c << F(" deg C") << endl;
}

// Print an integer in "00" format (with leading zero),
// followed by a delimiter character to Serial.
// Input value assumed to be between 0 and 99.
void printI00(int val, char delim)
{
    if (val < 10)
	    Serial << '0';
    Serial << _DEC(val);
    if (delim > 0)
	    Serial << delim;
    return;
}
// End of Serial printing routines

// EEPROM routines
boolean validSignature()
{
    byte by1 = EEPROM.read(EEBASE);
    byte by2 = EEPROM.read(EEBASE + 1);
    if ((by1 == SIG1) && (by2 == SIG2))
        return true;
    else
        return false;
}

void writeSignature()
{
    EEPROM.write(EEBASE,     SIG1);
    EEPROM.write(EEBASE + 1, SIG2);
}
// End EEPROM routines

// void    setTime(int hr,int min,int sec,int day, int month, int yr);
void setClock()
{
    time_t t;
    tmElements_t tm;
    
	Serial << F("Must set date and time") << endl << endl;
    Serial << F("Set LOCAL time (yy,m,d,h,m,s") << endl;
    
    while (!Serial.available());                 // Wait for serial input
    
    delay(10);                                   // may not be necessary
    
    // check for input to set the RTC, minimum length is 12, i.e. yy,m,d,h,m,s
    if (Serial.available() >= 12) {
        // note that the tmElements_t Year member is an offset from 1970,
        // but the RTC wants the last two digits of the calendar year.
        // use the convenience macros from the Time Library to do the conversions.
        int y = Serial.parseInt();
        if (y >= 100 && y < 1000)
            Serial << F("Error: Year must be two digits or four digits!") << endl;
        else {
            if (y >= 1000)
                tm.Year = CalendarYrToTm(y);
            else    // (y < 100)
                tm.Year = y2kYearToTm(y);
				
            tm.Month = Serial.parseInt();
            tm.Day = Serial.parseInt();
            tm.Hour = Serial.parseInt();
            tm.Minute = Serial.parseInt();
            tm.Second = Serial.parseInt();

            t = makeTime(tm);                    // This is local time
			
            time_t utc = myTZ.toUTC(t);          // convert to UTC
			
            RTC.set(utc);                        // Set the soft-clock
            setTime(utc);                        // Write to the hardwre clock
            RTC.get();                           // Get time in soft-clock from hardware clock
			
            writeSignature();                    // Write the signature bytes to EEPROM
            myTZ.writeRules(TZBASE);             // write rules to EEPROM
			
            Serial << endl << F("RTC set to: ZULU "); // Display set time on serial monitor
            printDateTime(utc);
			
            Serial << endl;
			
            // dump any extraneous input
            while (Serial.available() > 0)
                Serial.read();
        } // if (y >= 100 && y < 1000)
    } // if (Serial.available ...)
}

void showIntro(void) {
  // move the cursor to 0
  myLCD->clear();
  myLCD->setCursor(0, 0);
  myLCD->print("   Environmental    ");
  myLCD->setCursor(0, 1);
  myLCD->print("      Monitor       ");
  myLCD->setCursor(0, 2);
  myLCD->print("     Version 4      ");
  myLCD->setCursor(0, 3);
  myLCD->print(" Blackfire Software ");
}
