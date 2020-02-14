#include <Wire.h>
#include <SPI.h>
#include <RH_RF95.h>
#include "SparkFun_Ublox_Arduino_Library.h"
#include <MicroNMEA.h>
#include <nrf.h>
#include "nrf_timer.h"
#include "Timer.h"
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL343.h>

// Pin definitions
#define RFM95_RST 30
#define RFM95_CS 27
#define RFM95_INT 31
#define ACCEL_INT1 16 // accelerometer interrupt pin 1
#define ACCEL_INT2 15 // accelerometer interrupt pin 2
#define BATTERY_VOLTAGE (A0) // change dependent on PCB

// LoRa definitions
#define RF95_FREQ 915.0            // Can also be 434.0 - must match freq of chipset
RH_RF95 rf95(RFM95_CS, RFM95_INT); // Singleton instance of the radio driver

// GPS definitions/declerations
SFE_UBLOX_GPS myGPS;

// Accelerometer definitions/declerations
Adafruit_ADXL343 accel = Adafruit_ADXL343(12345);
int_config g_int_config_enabled = { 0 };
int_config g_int_config_map = { 0 };

// Timer definitions
#define nrf_timer_num   (1)
#define cc_channel_num  (0)
TimerClass timer(nrf_timer_num, cc_channel_num);

// Application variables
#define LORA_RX_DELAY 1 // how often data is sent over LoRa
#define LORA_TX_COUNT 5 // how many LORA_RX_DELAY intervals to wait before sending LoRa data
#define ACTIVITY_THRESH 100 // Set activity threshold: 62.5mg per increment
#define INACTIVITY_THRESH 50 // Set inactivity threshold: 62.5mg per increment
#define INACTIVE_TIME 10 // Amount of time (seconds), after inactivity is detected
#define GEOFENCE_LAT (434788942) // demo geofence lattitude
#define GEOFENCE_LONG (-805236027) // demo geofence longitude
#define GEOGENCE_RAD_CM 4000 // in CM
#define GEOGENCE_CONF 2 // Set the confidence level: 0=none, 1=68%, 2=95%, 3=99.7%, 4=99.99%
volatile uint8_t txCount = 0; // counter used to keep track of when we need to send LoRa message
long latitude_mdeg = 4347300;   // +43.473003 - default for demo
long longitude_mdeg = -8053948; // -80.539484 - default for demo
bool actFlag = 0; // determines whether active or not
bool inactFlag = 0; // determines whether inactive or not (may not be needed)
volatile bool timerFlag = 0; // determines whether timer has expired

// DEMO
#define BLUE_LED 19

// Timer ISR
void timer_isr() {
    timerFlag = 1;
    txCount++;
}

// Interrupt service routines for acceleromter
void accel_int1_isr(void)
{
    actFlag = 1;
    // clear interrupts?
}
void accel_int2_isr(void)
{
    inactFlag = 1;
    // clear interrupts?
}

void setup() 
{
  Serial.begin(9600);
  Serial.println("FindMe - Tag booted");

  // Demo: LEDs
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  setupGPS();
  setupLoRa();
  setupAccel();

  timer.attachInterrupt(&timer_isr, LORA_RX_DELAY*1000000); // microseconds
}

void loop()
{
  // TODO:  Check if USB is plugged in
  // If so, do not continue sending data packets until USB is unplugged
    // Maybe do - for demo purposes
  // While plugged in, monitor battery batt stat pin
  
  // Check to see if timer has expired
  if(timerFlag)
  {
      // Reset timer flag and timer
      timerFlag = 0;
      timer.attachInterrupt(&timer_isr, LORA_RX_DELAY*1000000); // microseconds
      
      // Check LoRa transciever for messages
      recieveLoRaPacket();
      
      // Check if we need to send new data
      if((txCount >= LORA_TX_COUNT) && (actFlag == 1))
      {
        txCount = 0; // reset TX flag
        sendLoRaPacket();
        actFlag = 0; // clear activity flag
        accel.checkInterrupts(); // clear interrupts
      }
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
    uint8_t len = sizeof(buf); // potentially useless line
    if (rf95.recv(buf, &len))
    { 
      // Print packet
      Serial.println("LoRa RX: ");
      Serial.println((char*)buf);

      // Check if forced request sent
      if(strncmp((char*)buf, "FRCD-RQST", 9) == 0)
      {
        Serial.println("Forced Request Recieved");
        sendLoRaPacket();
        timerFlag = 0; // reset flag
        txCount = 0; // reset TX flag
        timer.attachInterrupt(&timer_isr, LORA_RX_DELAY*1000000); // microseconds
      }
    }
  }     

  digitalWrite(LED_BUILTIN, LOW);
}

void sendLoRaPacket()
{
  digitalWrite(BLUE_LED, HIGH);

  // Enable transmitter
  delay(100);
  rf95.setModeTx();
  delay(100);

  // LoRa buffer
  uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
  buf[0] = '\0';

  // Get latest position
  // Make sure GPS has valid position
  // TODO: GPS should be able to get position in the background. we should not only be able to get it when it has a fix
  // TEST: Either see after first fix if this is a problem or revert to NMEA format
  Serial.print(" Lat1: "); Serial.print(myGPS.getLatitude()); Serial.print(" Long1: "); Serial.print(myGPS.getLongitude());
  if( myGPS.getFixType() > 0)
  {
    // Note: /100 needed to make sure its in format +xx.xxxxx
    latitude_mdeg = myGPS.getLatitude()/100;
    longitude_mdeg = myGPS.getLongitude()/100;
  }

  Serial.print("Lat: "); Serial.print(latitude_mdeg); Serial.print(" Long: "); Serial.print(longitude_mdeg);

  // Add +/- signs to coords
  // Negative coords already have negative sign
  if(latitude_mdeg > 0 && longitude_mdeg > 0){
    sprintf ((char*)buf, "+%ld+%ld", latitude_mdeg, longitude_mdeg);
  }
  else if(latitude_mdeg > 0 && longitude_mdeg < 0){
    sprintf ((char*)buf, "+%ld%ld", latitude_mdeg, longitude_mdeg);
  }
  else if(latitude_mdeg < 0 && longitude_mdeg > 0){
    sprintf ((char*)buf, "%ld+%ld", latitude_mdeg, longitude_mdeg);
  }
  else if(latitude_mdeg < 0 && longitude_mdeg < 0){
    sprintf ((char*)buf, "%ld%ld", latitude_mdeg, longitude_mdeg);
  }

  // Calculate battery percentage
  float ADC_RES = 1024.0; // default 10bit resolution - can be 8,10,12,14
  float REF_VOLTAGE = 3.6;
  float battVoltage; // battery + USB voltage
  int inputVoltageRaw = analogRead(BATTERY_VOLTAGE);
  battVoltage = (inputVoltageRaw / ADC_RES) * REF_VOLTAGE;

  // TODO: convert voltage to percentage   // Either use EXCEL or library
  uint8_t batteryPercentage = 99;
  // ESPERTO: battVoltage = 2*(inputVoltageRaw / ADC_RESOLUTION) * REFERENCE_VOLTAGE; // 2* because of voltage divider config

  // Append battery percentage and alert dignifier
  // Y = alert risen, N = no alert
  // Geofence status: 0 = unkown, 1 = inside, 2 = outside
  // \0 is needed for strlen to work properly
  // Get geofence status
  geofenceState currentGeofenceState; // Create storage for the geofence state
  myGPS.getGeofenceState(currentGeofenceState);
  Serial.print("GEO: "); Serial.println(currentGeofenceState.states[0]);
  if(currentGeofenceState.states[0] == 2)
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
  delay(100);
  rf95.setModeRx();
  delay(100);
  
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
    Serial.println(F("Ublox GPS not detected at default I2C address."));
    while (1);
  }

  // Limit I2C output to UBX (disable the NMEA noise)
  myGPS.setI2COutput(COM_TYPE_UBX);

  // Set dynamic mode of GPS
  // PORTABLE, STATIONARY, PEDESTRIAN, AUTOMOTIVE, SEA, AIRBORNE1g, AIRBORNE2g, AIRBORNE4g, WRIST, BIKE
  // For this application, PEDESTRIAN results in the lowest error
  myGPS.setDynamicModel(DYN_MODEL_PEDESTRIAN);

  // Clear all existing geofences.
  myGPS.clearGeofences();

  // Assign one geofence
  // Note: we can do up to 4
  myGPS.addGeofence(GEOFENCE_LAT, GEOFENCE_LONG, GEOGENCE_RAD_CM, GEOGENCE_CONF);

  // Save GPS settings
  myGPS.saveConfiguration();
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

  // Initialize LoRa
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

  // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on
  // The default transmitter power is 13dBm, using PA_BOOST.
  // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then 
  // you can set transmitter powers from 5 to 23 dBm:
  // False uses PA_BOOST pins (RF95)
  rf95.setTxPower(23, false);

  // Start transciever in RX mode
  rf95.setModeRx();
}

void setupAccel()
{
  // Initialize the sensor
  if(!accel.begin())
  {
    Serial.println("No ADXL343 detected");
    while(1);
  }

  // Setup Interrupt registers
  accel.writeRegister(ADXL343_REG_ACT_INACT_CTL, 0x77); // Detect activity/inactivity in all axis
  accel.writeRegister(ADXL343_REG_THRESH_ACT, ACTIVITY_THRESH); // Set activity threshold: 62.5mg per increment
  accel.writeRegister(ADXL343_REG_THRESH_INACT, INACTIVITY_THRESH); // Set inactivity threshold: 62.5mg per increment
  accel.writeRegister(ADXL343_REG_TIME_INACT, INACTIVE_TIME); // Amount of time (seconds), after inactivity is detected

  // Set range of accelerometer
  accel.setRange(ADXL343_RANGE_16_G);

  // Configure the HW interrupts
  pinMode(ACCEL_INT1, INPUT);
  pinMode(ACCEL_INT2, INPUT);
  attachInterrupt(digitalPinToInterrupt(ACCEL_INT1), accel_int1_isr, RISING);
  attachInterrupt(digitalPinToInterrupt(ACCEL_INT2), accel_int2_isr, RISING);

  // Enable interrupts and map them to their respective pins
  g_int_config_enabled.bits.activity  = true;
  g_int_config_enabled.bits.inactivity  = true;
  accel.enableInterrupts(g_int_config_enabled);
  g_int_config_map.bits.activity = ADXL343_INT1;
  g_int_config_map.bits.inactivity = ADXL343_INT2;
  accel.mapInterrupts(g_int_config_map);
}
