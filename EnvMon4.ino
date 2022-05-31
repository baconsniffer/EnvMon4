/*
 * Simple time/temperature logger
 *
 * Uses a DS3231 RTC and LCD display.
 * Self-adjusts for Daylight Savings Time
 * Readings stored on SD card every 5 minutes
 */

/*
   Hardware Setup

   DS3231  LCD     Arduino
   ======  ===     =======
     SDA   SDA       A4
     SCL   SCL       A5
	 
   DHT             Arduino
   ===             =======
    1                Vcc
    2                D8
    3                N/C
    4                Gnd

   SD CARD         Arduino
   =======         =======
    MOSI             D11
    MISO             D12
    CLK              D13
    CS               D10
*/

/*
 * Put includes here
 *
 */
#include <Wire.h>                 // I2C Communications
#include <EEPROM.h>               // Internal EEPROM for Timezone offsets
#include <LCD_I2C.h>              // HD44780 LCD with I2C backpack https://github.com/blackhack/LCD_I2C
#include <DHT.h>                  // DHT22 (AM2302) Temperature and Humidity Sensor https://github.com/adafruit/DHT-sensor-library
#include <DS3232RTC.h>            // For DS3231 https://github.com/JChristensen/DS3232RTC
#include <Streaming.h>            // http://arduiniana.org/libraries/streaming/
#include <Timezone.h>             // https://github.com/JChristensen/Timezone
#include <SPI.h>                  // SD card I/O
#include <SD.h>                   // SD card file system
/*
 * End of includes
 *
 */
 
/*
 * Put defines here
 *
 */

/* For the LCD via hardware I2C   */
#define SDA          A4           // Serial data
#define SCL          A5           // Serial clock
#define LCDADDR      0x27         // Backpack address
/* For the SD card                */
#define SDCS         10           // Chip Select pin to control SD reader
#define FILENAME     "ENVLOG.TXT" // SD card file name
/* For the EEPROM                 */
#define EEBASE       100          // Base address
#define SIG1         0x55         // Expected signature value at EEBASE
#define SIG2         0xAA         // Expected signature value at EEBASE + 1
#define TZBASE       EEBASE + 2   // Where Timezone rules are stored
/* For the 12/24 hour mode        */
#define HR12         0            // Show time in AM/PM format
#define HR24         1            // Use a 24 HR clock
/* For the DHT sensor             */
#define DHTPIN       8            // 1-wire interface to sensor
#define DHTTYPE      DHT22        // DHT22 (AM2302) sensor type
/* Sensor preferences             */
#define USEDHTTEMP   0b10000000   // Bit 7
#define USEDS32TEMP  0b01000000   // Bit 6
#define USEDHTHUM    0b00100000   // Bit 5
/* For UTC/Local display          */
#define MODEPIN      5            // Arduino digital pin
#define SHOWLOC      0            // Ground MODEPIN to show Local time
#define SHOWUTC      1            // Pull MODEPIN high to show UTC
/* Various delay values           */
#define HUMAN        1000         // Delay time to allow human to read LCD
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
  /*{0x00, 0x0C, 0x12, 0x0C, 0x00, 0x00, 0x00, 0x00},  // degree symbol */
  {0b01100,   // degree symbol
   0b10010,
   0b01100,
   0b00000,
   0b00000,
   0b00000,
   0b00000,
   0b00000}
};/*,
  {0x04, 0x0E, 0x1F, 0x04, 0x04, 0x04, 0x00, 0x00},  // up arrow
  {0x00, 0x00, 0x04, 0x04, 0x04, 0x1F, 0x0E, 0x04},  // down arrow
  {0x00, 0x1F, 0x11, 0x11, 0x11, 0x11, 0x1F, 0x00},  // rectangle
  {0x1F, 0x1E, 0x1C, 0x1A, 0x11, 0x00, 0x00, 0x00},  // up-left arrow
  {0x1F, 0x0F, 0x07, 0x0B, 0x11, 0x00, 0x00, 0x00},  // up-right arrow
  {0x00, 0x00, 0x00, 0x11, 0x1A, 0x1C, 0x1E, 0x1F},  // down-left arrow
  {0x00, 0x00, 0x00, 0x11, 0x0B, 0x07, 0x0F, 0x1F},  // down-right arrow
};
*/

/*
 * End of constants
 *
 */
    
/*
 * Global objects here
 *
 */

// Temperature and humidity sensor
DHT            dht(DHTPIN, DHTTYPE);

LCD_I2C        lcd(LCDADDR,16,2);  // set the LCD address to 0x27 for a 16 chars and 2 line display

// RTC
DS3232RTC      RTC;

// CDN Eastern Time Zone (Toronto)
TimeChangeRule myDST = {"EDT", Second, Sun, Mar, 2, -240};      //Daylight time = UTC - 4 hours
TimeChangeRule mySTD = {"EST", First,  Sun, Nov, 2, -300};      //Standard time = UTC - 5 hours
Timezone       myTZ(myDST, mySTD);                              // The default rules
// If TimeChangeRules are already stored in EEPROM they are read in setup().
TimeChangeRule *tcr;                                            //pointer to the time change rule, use to get TZ abbrev

/*
 * End of global objects
 *
 */
    
/*
 * Global simple variables here
*/

uint8_t         zone =     SHOWLOC;                             // Show local or UTC (local is default)
uint8_t         doLog =    1;                                   // Perform loggin to SD card
uint8_t         sensor  =  (USEDHTTEMP | USEDHTHUM);            // Which sensors are we using (USEDS32TEMP for RTC)
/*
 * End of global simple variables
 *
 */

/*
 * Global array variables here
*/

// Buffers to hold string representations of the date or time
//
// Looking at the DS3231 library it uses static arrays to
// turn date and time into strings. Perhaps just use some
// pointers to reference these ans save ourselvs some memory
char bufDate[16];                                               // the date YYYY-MM-DD
char bufTime[16];                                               // the time HH:MM:SS
char bufTemp[8];                                                // Temperature sxx in Celcius,
                                                                // optional leading negative
char bufHumid[8];                                               // Relative humidity 0-100

/*
 * End of global array variables
 *
 */

 
/*
 * Setup code
 *
 */

void setup() {
   // Configure UTC/Local mode pin
   pinMode(MODEPIN, INPUT_PULLUP);

   zone = adjustMode();
	
   // Configure tell-tale (on-board LED)
   pinMode(13, OUTPUT);
   digitalWrite(13, LOW); 
	
   Serial.begin(115200);
   Serial << endl << F("Blackfire Software") << endl << F("Startup ...") << endl;
    
   // Activate LCD module
   lcd.begin();                    // Initialize the display
   lcd.backlight();                // Turn on backlight during startup
   showIntro();                    // Display the splash screen

   // register the custom bitmaps
   int i;
   int bitmap_size = sizeof(my_bitmap) / sizeof(my_bitmap[0]);
  
   for (i = 0; i < bitmap_size; i++) {
       lcd.createChar(i, (uint8_t *)my_bitmap[i]);
   }

   // Un-comment to force update for testing
   // setClock();
    
   // See if we have stored valid DST rules in EEPROM
   if (validSignature()) {    // Fetch rules from EEPROM
      Serial << F("EEPROM Valid") << endl;
      lcd.clear(); lcd.setCursor(0,0);
      lcd.print(F("EEPROM Valid"));
      delay(HUMAN);
      
      Timezone myTZ(EEBASE+2);
      Serial << F("Read Timezone Data") << endl;
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print(F("Timezone"));
      lcd.setCursor(0,1);
      lcd.print(F("info loaded"));
      delay(HUMAN);
   } else {                   // Go into timeset mode
      Serial << F("EEPROM Invalid") << endl << F("Setting time") << endl;
      lcd.clear(); lcd.setCursor(0, 0);
      lcd.print(F("EEPROM Invalid"));
      lcd.setCursor(0,1);
      lcd.print(F("Set Time"));
      setClock();
   }

   // setSyncProvider() causes the Time library to synchronize with the
   // external RTC by calling RTC.get() every five minutes by default.
   setSyncProvider(RTC.get);
	
   // 3600 s (1 h) between syncs with hardware clock
   setSyncInterval((time_t)3600);

   Serial << F("RTC Sync");
   lcd.clear(); lcd.setCursor(0, 0);
   lcd.print(F("RTC Sync"));
   delay(HUMAN);
  
   if (timeStatus() != timeSet) {    // Go into timeset mode
      Serial << F("RTC FAIL!");
      lcd.clear(); lcd.setCursor(0, 0);
      lcd.print(F("RTC FAIL!"));
      lcd.setCursor(0,1);
      lcd.print(F("Set Time"));
      setClock();
   }
   Serial << endl;

   // Set soft RTC from hardware clock
   RTC.get();

   // Activate the DHT22 sensor if any of its sensors are used
   Serial << F("DHT ");
   if ((sensor & USEDHTHUM) | (sensor & USEDHTTEMP)) {  // If we are using the DHT, initialize it
      dht.begin();
      Serial << F("init");
      lcd.clear();
      lcd.print(F("DHT init"));
   } else {
      Serial << F("not used");
      lcd.clear();
      lcd.print(F("DHT not used"));
   }
   Serial << endl;
   delay(HUMAN); // Give the human a chance to read the screen

   mkTempStr(sensor);                              // Read temperature and convert to string

   Serial << F("Temperature: ");
   if (sensor & USEDS32TEMP) {                  // If both temperature sensors are selected, DS3231 is preferred
      Serial << F("DS3231");
   } else if (sensor & USEDHTTEMP) {            // Use the DHT sensor
      Serial << F("DHT");
   } else {
      Serial << F("None");
   }
   Serial << endl;

   // Initialize the DHT temperature and humidity sensor
   Serial << F("Humidity: ");
   // Load initial value from the humidity sensor, if used
   mkHumidStr(sensor);
   if (sensor & USEDHTHUM) {
      Serial << F("Used");
   } else {
      Serial << F("None");
   }
   Serial << endl;

   // see if the card is present and can be initialized:
   if (!SD.begin(SDCS)) {
      Serial << F("SD Card failed, or not present") << endl;
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print(F("SD Failure"));
      lcd.setCursor(0, 1);
      lcd.print(F("No Logging"));
      // don't do anything more:
      doLog = 0;
   } else {
      Serial << F("SD initialized.") << endl;
      lcd.clear(); lcd.setCursor(0,0);
      lcd.print(F("SD Init"));
   }
   delay(HUMAN);
   
   lcd.clear();
   lcd.backlight();   // Ensure the light is on

   // RTC is up at this point and the display and sensors are ready
   // Attach RTC interrupt to pin 2 which connect to SQW on the RTC
   // SQW will toggle at 1 Hz and provide updates to the time and display

}

/*
 * Main program loop
 *
 */
void loop() {
   zone = adjustMode();                                // See if we display local or UTC
   static uint8_t first = 1;                           // Set flag to force console update and SD log on startup
//   static uint8_t bk    = BKDELAY;
   static time_t tLast;                                // time of last loop iteration
   // var to hold current time
   time_t t;

   if (Serial.available())             // if the user is inputting data they want to set the time
      setClock();

   // Read UTC time
   time_t utc = now();
   Serial << F("time_t utc = ") << utc << endl;
	
   // Convert to local
   t = myTZ.toLocal(utc, &tcr);
    
   if (t != tLast) {                       // if the time has changed, do some stuff (resolution of time_t is 1 sec)
      mkDateStr(utc);                      // make date and time strings from utc representation
      mkTimeStr(utc);

      if (first || (second(utc) == 0) && (minute(utc) % 5) == 0) {        // Every 5 minutes, log to SD card and update humidity
         first = 0;                        // After the first update we do it every five minutes
         mkHumidStr(sensor);               // Update the humidity reading (sensor is slow)
         mkTempStr(sensor);                // Take a temperature reading based on sensor
         
         if (doLog) {
            Serial << F("SD Logging now ...") << endl;
            sdLog();                       // Write the data to the SD card
            Serial << F("Logging done") << endl;
         } // doLog
         consoleUpdate();                  // send date and time to serial (always utc)
      }  // first or five minutes elapsed

      // All logging to console and SD card is done in UTC
      // After we are finished with the UTC representations of the date and time
      // check to see if we are displaying the local time and make necessary
      // updates to date and time.
      if (zone == SHOWLOC) {               // if local time is displayed on the screen
         mkDateStr(t);                     // Get date
         mkTimeStr(t);                     // and time strings based on local time
      }

      displayDate();                       // Show date and time on the LCD
      displayTime();
      displayTemp();                       // show temperature on the LCD
      displayHumid();                      // show humidity on the LCD

      tLast = t;                           // remember this time through the loop
   } // if (t != tLast)
}
// ---------- End of main program loop ----------

/*
 * Helper functions - could be broken out into a separate .cpp and .h filebuf
 */
//-------- On fatal errors flash the on-board LED --------
void halt(int ontime, int offtime) {
    while(1) {
        digitalWrite(13, HIGH);
        delay(ontime);
        digitalWrite(13, LOW);
        delay(offtime);
    } //while
} // halt()
//--------------------------------------------------------
//
// ---------- Start Date Related Functions ----------
/*
 * Take the date value and turn it into a string representation
 * in bufDate[] of the form YYYY-MM-DD
 */
void mkDateStr(time_t t) {
   uint16_t yr   = year(t);
   uint8_t  mon  = month(t);
   uint8_t  dy   = day(t);
   
   sprintf(bufDate, "%04d-%02d-%02d", yr, mon, dy);
}

/*
 * Show the date on the display
 */
void displayDate() {
   lcd.setCursor(0, 1);    // Column 0, Row 1 (under the time)
   lcd.print(bufDate);
}

/*
 * Send the date to the serial console
 */
 void consoleDate() {
   Serial << bufDate ;
 }
// ---------- End Date Related Functions ----------
//

// ---------- Start Time Related Functions ----------
/*
 * Take the time value and turn it into a string representation
 * in bufTime[] of the form HH:MM:SS
 */
void mkTimeStr(time_t t) {
	uint8_t  hr  = hour(t);
	uint8_t  min = minute(t);
	uint8_t  sec = second(t);
	
	sprintf(bufTime, "%02d:%02d:%02d", hr, min, sec);
}

/*
 * Display the hours and minutes on the display
 */
void displayTime() {
   lcd.setCursor(0, 0);   // Row 0, column 0, upper left corner
   lcd.print(bufTime);
}

/*
 * Send the time to the serial console
 */
void consoleTime() {
   Serial << bufTime;
}
// ---------- End Time Related Functions ----------
//

//
// ---------- Start Temperature Functions ----------

/*
 * Read the sensor passed in and make a temperature reading
 */
void mkTempStr(uint8_t sensor) {
   float c;                             // Our temperature
   int i = 0; int f = 0;                // Some working values cuz i don't understand how sprintf does floats

   bufTemp[0] = '*'; bufTemp[1] = '*'; bufTemp[2] = '\0';
   if (sensor & USEDHTTEMP) {
      c = dht.readTemperature();
   }
   if (sensor & USEDS32TEMP) {
      c = RTC.temperature() / 4.;
   }
   if (!isnan(c)) {  // weird logic but if c got a real value;
      i = (int)c;                          // get the integer part
      c = c - (float)i;                    // have c only hold the fractional part
      c = 100.0 * c;                       // get the fractional part as an integer
      f = (int)c;
      sprintf(bufTemp, "%d.%d", i, f);     // Two digits of temperature with 2 decimal
   }
} // either way, bufTemp has something in it

/*
 * Show the temperature
 */
void displayTemp() {
   lcd.setCursor(10, 0);                // Row 1, column 12 (upper right of display)
   lcd.print("      ");                 // Erase old reading
   lcd.setCursor(10, 0);
   lcd.print(bufTemp);                  // Display new reading
   lcd.write(0);                        // Add degree symbol (print custom chars with .write method
   lcd.print("C");
}

/*
 * Write the temperature to the serial console
 */
void consoleTemp() {
   Serial << bufTemp;
}

// ---------- End Temperature Functions ----------
//

//
// ---------- Start Humidity Related Functions ----------
void mkHumidStr(uint8_t sensor) {
   float h;
   int i, f;

   bufHumid[0] = '*'; bufHumid[1] = '*'; bufHumid[2] = '\0';
   
   if (sensor & USEDHTHUM) {
      h = dht.readHumidity();
   } // Add else if clauses to accomodate more sensors
   // Check if any reads failed and exit early (to try again).
   if (isnan(h)) {
      Serial << (F("Failed to read from DHT sensor!")) << endl;
   } else {
      i = (int)h;   // cast to int to avoid sprintf float bug
      f = int(10.0 * (h - float(i)));
      sprintf(bufHumid,"%d.%1d", i, f);
   }
   return;
}

void displayHumid() {
   lcd.setCursor(11, 1);   // column 11 of the 2nd row
   lcd.print("     ");
   lcd.setCursor(11, 1);   // column 11 of the 2nd row
   lcd.print(bufHumid);
   lcd.print('%');
}

void consoleHumid() {
   Serial << bufHumid;
}

// ---------- End Humidity Related Functions ----------
//

/*
 * The following routines are for the serial port
 */
 // print date and time to Serial in ISO8601 format
void consoleUpdate() {
   consoleDate();                    // Print date
   Serial << "T";                    // ISO8601
   consoleTime();                    // Print time
   Serial << "Z";                    // Append a Z for ISO8601
   Serial << ",";
   consoleTemp();                    // send temperature to serial
   Serial << ",";
   consoleHumid();                   // send humidity to console
   Serial << endl;                   // spit out a newline at the end
}

//----------------------------------------------------------------------------
void sdLog() { 
   File dataFile = SD.open(FILENAME, FILE_WRITE);
   // if the file is available, write to it:
   if (dataFile) {
      dataFile.print(bufDate);       //Date and time are ISO8601 format
      dataFile.print("T");
	    dataFile.print(bufTime);
	    dataFile.print("Z");           // Indicate time is UTC (Zulu time)
	    dataFile.print(",");           // Delimiter for csv format
	    dataFile.print(bufTemp);       // Temperature from RTC
      dataFile.print(",");
      dataFile.println(bufHumid);    // humidity (add newline at the end)
      dataFile.close();
   }
   // if the file isn't open, pop up an error:
   else {
      Serial << F("error opening ") << FILENAME << endl;
	    halt(75,200);
   }
}
   
//----------------------------------------------------------------------------

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
         consoleUpdate();
			
         Serial << endl;
			
         // dump any extraneous input
         while (Serial.available() > 0)
            Serial.read();
      } // if (y >= 100 && y < 1000)
   } // if (Serial.available ...)
}

void showIntro(void) {
  // Show a startup message
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System");
  lcd.setCursor(0, 1);
  lcd.print("Startup");
  delay(HUMAN);
}

// Return whether we are displaying local or UTC time
int adjustMode() {
   return(digitalRead(MODEPIN) ? SHOWLOC : SHOWUTC);
}
