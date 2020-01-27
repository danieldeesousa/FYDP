#include <Wire.h>
#include <SPI.h>
#include <RH_RF95.h>
#include "SparkFun_Ublox_Arduino_Library.h"
#include <MicroNMEA.h>
#include <nrf.h>
#include "nrf_timer.h"
#include "Timer.h"

// Pin definitions
#define RFM95_RST 30
#define RFM95_CS 27
#define RFM95_INT 31

// LoRa definitions
#define RF95_FREQ 915.0            // Can also be 434.0 - must match freq of chipset
RH_RF95 rf95(RFM95_CS, RFM95_INT); // Singleton instance of the radio driver

// GPS definitions/declerations
SFE_UBLOX_GPS myGPS;
char nmeaBuffer[100];
MicroNMEA nmea(nmeaBuffer, sizeof(nmeaBuffer));

// Accelerometer Definitions

// Timer definitions
#define nrf_timer_num   (1)
#define cc_channel_num  (0)
TimerClass timer(nrf_timer_num, cc_channel_num);

// Application variables
#define LORA_RX_DELAY 1 // how often data is sent over LoRa
#define LORA_TX_COUNT 5 // how many LORA_RX_DELAY intervals to wait before sending LoRa data
volatile uint8_t txCount = 0;
float latitude_mdeg = 4347300;   // +43.473003 - default for demo
float longitude_mdeg = -8053948; // -80.539484 - default for demo
bool isActive; // determines whether device is active (1) or inactive (0)
bool activityFlag; // used to determine when device has changed activity state
bool isAlert = 0; // determines whether tag is outside geofence (0 = NO, 1 = YES)
volatile bool timerFlag = 0; // determines whether timer has expired

// DEMO
#define BLUE_LED 19

void Timer_callback() {
    timerFlag = 1;
    txCount++;
}

void setup() 
{
  Serial.begin(115200);
  Serial.println("FindMe - Tag booted");

  // Demo: LEDs
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  setupGPS();
  setupLoRa();

  timer.attachInterrupt(&Timer_callback, LORA_RX_DELAY*1000000); // microseconds
}

void loop()
{
  // Check if we need to check LoRa transciever for messages
  if(timerFlag)
  {
      timerFlag = 0; // reset flag
      timer.attachInterrupt(&Timer_callback, LORA_RX_DELAY*1000000); // microseconds
      recieveLoRaPacket();
  }

  // Check if we need to send new data
  if(txCount == LORA_TX_COUNT)
  {
    txCount = 0; // reset TX flag
    timer.attachInterrupt(&Timer_callback, LORA_RX_DELAY*1000000); // microseconds
    sendLoRaPacket();
  }
}

void recieveLoRaPacket()
{
  digitalWrite(LED_BUILTIN, HIGH);

  // Check for LoRa message
  if (rf95.available())
  {
    // Should be a message for us now
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);
    if (rf95.recv(buf, &len))
    { 
      Serial.println("LoRa RX: ");
      Serial.println((char*)buf);

      // Check if forced request sent
      if(strncmp((char*)buf, "FRCD-RQST", 9) == 0)
      {
        Serial.println("Forced Request Recieved");
        sendLoRaPacket();
        timerFlag = 0; // reset flag
        txCount = 0; // reset TX flag
        timer.attachInterrupt(&Timer_callback, LORA_RX_DELAY*1000000); // microseconds
      }
    }
  }     

  digitalWrite(LED_BUILTIN, LOW);
}

void sendLoRaPacket()
{
  digitalWrite(BLUE_LED, HIGH);

  // Enable transmitter
  rf95.setModeTx();
  
  // Get updated GPS data
  myGPS.checkUblox();
  Serial.println(myGPS.getNavigationFrequency());
  // Check if data is valid
  if(nmea.isValid() == true)
  {
    // Get current lat/long
    // Note: /10.0 needed to make sure its in format +xx.xxxxx
    latitude_mdeg = nmea.getLatitude()/10.0;
    longitude_mdeg = nmea.getLongitude()/10.0;
  }

  // Place lat/long data into buffer to be sent over LoRa
  uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];

  // Add +/- signs to coords
  // Negative coords already have negative sign
  if(latitude_mdeg > 0 && longitude_mdeg > 0){
    sprintf ((char*)buf, "+%0.0f+%0.0f", latitude_mdeg, longitude_mdeg);
  }
  else if(latitude_mdeg > 0 && longitude_mdeg < 0){
    sprintf ((char*)buf, "+%0.0f%0.0f", latitude_mdeg, longitude_mdeg);
  }
  else if(latitude_mdeg < 0 && longitude_mdeg > 0){
    sprintf ((char*)buf, "%0.0f+%0.0f", latitude_mdeg, longitude_mdeg);
  }
  else if(latitude_mdeg < 0 && longitude_mdeg < 0){
    sprintf ((char*)buf, "%0.0f%0.0f", latitude_mdeg, longitude_mdeg);
  }

  // Calculate battery percentage
  float ADC_RES = 1024.0; // default 10bit resolution - can be 8,10,12,14
  float REF_VOLTAGE = 3.6;
  float battVoltage; // battery + USB voltage
  int inputVoltageRaw = analogRead(A0); // define based on PCB pinout???
  battVoltage = (inputVoltageRaw / ADC_RES) * REF_VOLTAGE;

  // TODO: convert voltage to percentage   // Either use EXCEL or library
  uint8_t batteryPercentage = 99 + isAlert;
  // ESPERTO: battVoltage = 2*(inputVoltageRaw / ADC_RESOLUTION) * REFERENCE_VOLTAGE; // 2* because of voltage divider config

  // Append battery percentage and alert dignifier
  // Y = alert risen, N = no alert
  // /0 is needed for strlen to work properly
  // TODO; find here if outside of geofence?
  isAlert = ! isAlert; // replace with GPS
  if(isAlert)
  {
     sprintf ((char*)buf, "%s %d Y\0", (char*)buf, batteryPercentage);
  }
  else
  {
    sprintf ((char*)buf, "%s %d N\0", (char*)buf, batteryPercentage);
  }

  // Send LoRa message with GPS coordinates, battery info, 
  rf95.send(buf, strlen((char*)buf));
  delay(10);
  rf95.waitPacketSent(); // blocks until transmitter is no longer transmitting
  Serial.print("LoRa TX: ");
  Serial.println((char*)buf);
  
  // Put transciever back in RX mode
  rf95.setModeRx();
  
  digitalWrite(BLUE_LED, LOW);
}







/////////////////////////////////////////////////////////////
//////////// HELPER FUNCTIONS ///////////////////////////////
/////////////////////////////////////////////////////////////

// Turns GPS on
void setupGPS()
{
  // Setup GPS
  Wire.begin();
  if (myGPS.begin() == false)
  {
    Serial.println(F("Ublox GPS not detected at default I2C address. Please check wiring. Freezing."));
    while (1);
  }
}

void setupLoRa()
{
  // Setup LoRa
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);
  // Manual reset
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);
  while (!rf95.init()) {
    Serial.println("LoRa radio init failed");
    Serial.println("Uncomment '#define SERIAL_DEBUG' in RH_RF95.cpp for detailed debug info");
    while (1);
  }
  Serial.println("LoRa radio init OK!");
  // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
  if (!rf95.setFrequency(RF95_FREQ)) {
    Serial.println("setFrequency failed");
    while (1);
  }
  Serial.print("Set Freq to: "); Serial.println(RF95_FREQ);
  // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on
  // The default transmitter power is 13dBm, using PA_BOOST.
  // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then 
  // you can set transmitter powers from 5 to 23 dBm:
  rf95.setTxPower(23, false);

  // Start transciever in RX mode
  rf95.setModeRx();
}

//This function gets called from the SparkFun Ublox Arduino Library
//As each NMEA character comes in you can specify what to do with it
//Useful for passing to other libraries like tinyGPS, MicroNMEA, or even
//a buffer, radio, etc.
void SFE_UBLOX_GPS::processNMEA(char incoming)
{
  //Take the incoming char from the Ublox I2C port and pass it on to the MicroNMEA lib
  //for sentence cracking
  nmea.process(incoming);
}
