#include "Adafruit_FONA.h"

#define FONA_RX 2
#define FONA_TX 3
#define FONA_RST 4

#include <SoftwareSerial.h>
SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaSS;

#include <avr/sleep.h>
#include <avr/power.h>

Adafruit_FONA fona = Adafruit_FONA(FONA_RST);

#define samplingRate 120 // The time in between posts, in seconds

uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);

uint8_t type;

float latitude, longitude, speed_kph, heading, altitude;

char imei[16] = {0}; // MUST use a 16 character buffer for IMEI!

bool pinEntered = false;

void setup() {
  while (!Serial);

  Serial.begin(115200);
  Serial.println(F("FONA basic test"));
  Serial.println(F("Initializing....(May take 3 seconds)"));

  fonaSerial->begin(4800);
  if (! fona.begin(*fonaSerial)) {
    Serial.println(F("Couldn't find FONA"));
    while (1);
  }
  type = fona.type();
  Serial.println(F("FONA is OK"));
  Serial.print(F("Found "));
  switch (type) {
    case FONA800L:
      Serial.println(F("FONA 800L")); break;
    case FONA800H:
      Serial.println(F("FONA 800H")); break;
    case FONA808_V1:
      Serial.println(F("FONA 808 (v1)")); break;
    case FONA808_V2:
      Serial.println(F("FONA 808 (v2)")); break;
    case FONA3G_A:
      Serial.println(F("FONA 3G (American)")); break;
    case FONA3G_E:
      Serial.println(F("FONA 3G (European)")); break;
    default: 
      Serial.println(F("???")); break;
  }
  
  // Print module IMEI number.
  uint8_t imeiLen = fona.getIMEI(imei);
  if (imeiLen > 0) {
    Serial.print("Module IMEI: "); Serial.println(imei);
  }

  fona.setGPRSNetworkSettings(F("internet"), F(""), F("")); // provider specific, for coop mobile/salt prepaid
  fona.setHTTPSRedirect(true);
}

void loop() {
  while (!fona.enableGPS(true)) {
    Serial.println(F("Failed to turn on GPS, retrying..."));
    delay(2000); // Retry every 2s
  }
  Serial.println(F("Turned on GPS!"));

  // Get a fix on location, try every 2s
  uint8_t counter = 0;
  while (counter < 12 && !fona.getGPS(&latitude, &longitude, &speed_kph, &heading, &altitude)) {
    Serial.println(F("Failed to get GPS location, retrying..."));
    delay(20000); // Retry every 20s
    counter++;
  }

  char dateBuff[20];
  uint8_t dateLength = getGPSDate(dateBuff, 20);

  bool fixture = counter < 12;
  if (fixture) {
    Serial.println(F("Fixed it!"));
    Serial.println(F("---------------------"));
    Serial.print(F("Latitude: ")); Serial.println(latitude, 6);
    Serial.print(F("Longitude: ")); Serial.println(longitude, 6);
    Serial.print(F("Speed: ")); Serial.println(speed_kph);
    Serial.print(F("Heading: ")); Serial.println(heading);
    Serial.print(F("Altitude: ")); Serial.println(altitude);
    if (dateLength) {
      Serial.print(F("Date: ")); Serial.println(dateBuff);
    }
    Serial.println(F("---------------------"));
  }
  else {
    Serial.println("Failed to get fixture");
  }

  if (!pinEntered) {
    if (!netStatus()) {
      while (!setPIN()) {
        Serial.println(F("Failed to set PIN, retrying..."));
        delay(60000); // Retry every 60s
        if (netStatus()) {
          break;
        }
      }
      Serial.println(F("Entered PIN!"));
      pinEntered = true;
      delay(60000); // Wait for registration 60s
    }
  }

  while (!netStatus()) {
    Serial.println(F("Failed to connect to cell network, retrying..."));
    delay(2000); // Retry every 2s
  }
  Serial.println(F("Connected to cell network!"));

  // Disable GPRS just to make sure it was actually off so that we can turn it on
  if (!fona.enableGPRS(false)) Serial.println(F("Failed to disable GPRS!"));
  
  // Turn on GPRS
  while (!fona.enableGPRS(true)) {
    Serial.println(F("Failed to enable GPRS, retrying..."));
    delay(2000); // Retry every 2s
  }
  Serial.println(F("Enabled GPRS!"));

  // POST data, TODO: switch to actual POST
  char URL[255]; // Make sure this is long enough for your request URL
  if (fixture) {
    char latBuff[16], longBuff[16], locBuff[64], speedBuff[16],
         headBuff[16], altBuff[16], tempBuff[16], battBuff[16];
  
    // Format the floating point numbers
    dtostrf(latitude, 1, 6, latBuff);
    dtostrf(longitude, 1, 6, longBuff);
    dtostrf(speed_kph, 1, 0, speedBuff);
    dtostrf(heading, 1, 0, headBuff);
    dtostrf(altitude, 1, 1, altBuff);
  
    // Also construct a combined, comma-separated location array
    // (many platforms require this for dashboards, like Adafruit IO):
    sprintf(locBuff, "%s,%s,%s,%s", speedBuff, latBuff, longBuff, altBuff); // This could look like "10,33.123456,-85.123456,120.5"
    
    sprintf(URL, "https://kasterpillar.com/bin/server/insert.php?user=No%%20ones%%20land&content=IMEI:%s,lat:%s,long:%s,speed:%s,head:%s,alt:%s,date:%s", imei, latBuff, longBuff,
            speedBuff, headBuff, altBuff, dateLength ? dateBuff : "");
  }
  else {
    sprintf(URL, "https://kasterpillar.com/bin/server/insert.php?user=No%%20ones%%20land&content=IMEI:%s,failure:Fixture", imei);
  }

  Serial.println("URL getting:");
  Serial.println(URL);
  httpGet(URL);

  //disableGPS();
  
  Serial.print(F("Waiting for ")); Serial.print(samplingRate); Serial.println(F(" seconds\r\n"));
  delay(samplingRate * 1000UL);
}

bool httpGet(char* URL) {
  int16_t length;
  uint16_t statuscode;
  Serial.println(F("****"));
  if (!fona.HTTP_GET_start(URL, &statuscode, (uint16_t *)&length)) {
    Serial.println("Failed!");
    Serial.println(statuscode);
    return false;
  }
  while (length > 0) {
    while (fona.available()) {
      char c = fona.read();

      // Serial.write is too slow, we'll write directly to Serial register!
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
      loop_until_bit_is_set(UCSR0A, UDRE0); /* Wait until data register empty. */
      UDR0 = c;
#else
      Serial.write(c);
#endif
      length--;
      if (! length) break;
    }
  }
  Serial.println(F("\n****"));
  fona.HTTP_GET_end();
  return true;
}

bool netStatus() {
  int n = fona.getNetworkStatus();
  
  Serial.print(F("Network status ")); Serial.print(n); Serial.print(F(": "));
  if (n == 0) Serial.println(F("Not registered"));
  if (n == 1) Serial.println(F("Registered (home)"));
  if (n == 2) Serial.println(F("Not registered (searching)"));
  if (n == 3) Serial.println(F("Denied"));
  if (n == 4) Serial.println(F("Unknown"));
  if (n == 5) Serial.println(F("Registered roaming"));

  if (!(n == 1 || n == 5)) return false;
  else return true;
}

bool setPIN() {
  if (! fona.unlockSIM("XXXX")) {
    Serial.println(F("Failed"));
    return false;
  }
  return true;
}

void disableGPS() {
  while (!fona.enableGPS(false)) {
    Serial.println(F("Failed to turn off GPS, retrying..."));
    delay(2000); // Retry every 2s
  }
  Serial.println(F("Turned off GPS!"));
}

// Contribute back to library... if it would be adapted to all devices
uint8_t getGPSDate(char *buffer, uint8_t maxbuff) {
  if (fona.type() != FONA808_V2) {
    return 0;
  }

  char gpsbuffer[120];
  uint8_t res_len = fona.getGPS(32, gpsbuffer, 120);

  if (res_len == 0)
    return 0;
    
  // skip GPS run status
  char *tok = strtok(gpsbuffer, ",");
  if (! tok) return 0;

  // skip fix status
  tok = strtok(NULL, ",");
  if (! tok) return 0;

  // take the date
  tok = strtok(NULL, ",");
  if (! tok) return 0;

  uint8_t len = max(maxbuff-1, (int)strlen(tok));
  strncpy(buffer, tok, len);
  buffer[len] = 0;
  return len;
}
