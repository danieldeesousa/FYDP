#include <string.h>
#include <bluefruit.h>
#include <SPI.h>
#include <RH_RF95.h>

// Disable/enable printlns
//#define Sprintln(a) (Serial.println(a)) // uncomment to ENABLE printlns
#define Sprintln(a) // uncomment to DISABLE printlns

// Pin definitions
#define RFM95_RST 30
#define RFM95_CS 27
#define RFM95_INT 31
#define BATTERY_VOLTAGE (A3) // senses battery voltage/USB rail
#define TAG_CHARGE_DET (A4) // senses whether tag is plugged in
#define TAG_CHARGE_COMP (A5) // senses whether tag is done charging
#define TAG_CHARGE_LED (25) // indicates tag charging activity
#define BLE_LED (19) // driven by low-level BLE lib

// BLE services
BLEDfu bledfu; // OTA DFU service
BLEUart bleuart; // Uart over BLE service

// LoRa definitions
#define RF95_FREQ (915.0) // frequency of LoRa
RH_RF95 rf95(RFM95_CS, RFM95_INT); // singleton instance of the LoRa driver

// Analog definitions
#define ADC_RES (1024.0) // default 10bit resolution - can be 8,10,12,14
#define REF_VOLTAGE (3.6) // internal ADC reference voltage
#define VOLTAGE_USB (4.5) // voltage detected when USB is in
#define VOLTAGE_DET_TAG (1.0) // HIGH voltage of hub-tag charging interface

// Serial port variables
static char input[20]; // serial buffer
static int idx = 0; // current index in serial buffer
#define FRCD_RQST_SIZE (9) // number of chars in "FRCD-RQST"

// Application variables
#define RSSI_BUF_SIZE 1 // TODO to implement LPF
int16_t rssiBuf[RSSI_BUF_SIZE]; // used to average RSSI value
int8_t rssiBuf_idx = 0;
int16_t averageRSSI; // RSSI = Received Signal Strength Indicator
bool isBLEConnected = 0; // 1 if connected, 0 if not

// BLE connection callbacks
void prph_connect_callback(uint16_t conn_handle)
{
  isBLEConnected = true;
}
void prph_disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  isBLEConnected = false;
}

void setup(void)
{
  Serial.begin(9600);
  Sprintln("FindMe - Hub booted");

  // Enable LEDs
  pinMode(TAG_CHARGE_LED, OUTPUT);
  digitalWrite(TAG_CHARGE_LED, LOW);
  pinMode(BLE_LED, OUTPUT);

  // Setup BLE and LoRa
  setupBLE();
  setupLoRa();
}

void loop(void)
{
  // TAG LED charging behaviour: flashing (charging), solid (charge complete)
  // Check if hub is powered, tag is plugged in, and done charging
  // 2* because of resistor divider (BATTERY_VOLTAGE)
  // TAG_CHARGE_DET is biased at 1.66V and is pulled down when tag is plugged in
  // TAG_CHARGE_COMP is pulled low by default and is driven high when the tag is done charging
  float battVoltage = 2*(analogRead(BATTERY_VOLTAGE) / ADC_RES) * REF_VOLTAGE;
  float tagChargeVoltage = (analogRead(TAG_CHARGE_DET) / ADC_RES) * REF_VOLTAGE;
  float tagCompVoltage = (analogRead(TAG_CHARGE_COMP) / ADC_RES) * REF_VOLTAGE;

  // Check if hub is powered and whether tag is connected
  if((battVoltage > VOLTAGE_USB) && (tagChargeVoltage < VOLTAGE_DET_TAG))
  {
    digitalWrite(TAG_CHARGE_LED, HIGH);
  }
  
  // Check LoRa
  recieveLoRaPacket();
  delay(1000);

  // Check if tag charging is complete (do not turn off LED when charge complete)
  // Turn off LED if USB-B is no longer powering hub OR if tag is disconnected OR if tag is still charging
  if((battVoltage < VOLTAGE_USB) || (tagChargeVoltage > VOLTAGE_DET_TAG) || (tagCompVoltage < VOLTAGE_DET_TAG))
  {
    digitalWrite(TAG_CHARGE_LED, LOW);
  }
  
  // Check ESP Serial port
  readSerial();
  delay(1000); 
}

void readSerial()
{
  // check if serial data is available
  while(Serial.available() > 0)
  {
    // append data to array
    input[idx] = Serial.read();
    idx++;
    
    // check if end of message
    if(input[idx-1] == '\n')
    {
      // check if forced request sent from ESP
      input[idx-1] = '\0';
      if(strncmp(input, "FRCD-RQST", FRCD_RQST_SIZE) == 0)
      {
        sendForcedRqst();
      }

      // clear buffer
      idx = 0;
      input[0] = '\0';
    }

    // check for buffer overflow
    if(idx >= (FRCD_RQST_SIZE+1))
    {
      // clear buffer
      idx = 0;
      input[0] = '\0';
    }
  }
}

void recieveLoRaPacket()
{ 
  // Wait for LoRa message
  if (rf95.available())
  {
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);
    if (rf95.recv(buf, &len))
    {
      // Terminate message
      buf[len] = '\0';
      
      // Print packet
      Sprintln("LoRa RX: ");
      Sprintln((char*)buf);

      // Send LoRa packet to ESP to process
      Serial.write((char*)buf);
      Serial.write("\n");

      // Determine whether to send packet over BLE
      if(isBLEConnected == true)
      {
        // Remove geo-fence indicator
        buf[len-1] = '\0';
        
        // Get RSSI
        Sprintln(rf95.lastRssi());
        rssiBuf[rssiBuf_idx++] = rf95.lastRssi();
        rssiBuf_idx %= RSSI_BUF_SIZE;
    
        // Calcute average RSSI
        averageRSSI = 0; // reset
        for (int i = 0; i < RSSI_BUF_SIZE; i++){
           averageRSSI += rssiBuf[i]; // add up all RSSIs
        }
        averageRSSI /= RSSI_BUF_SIZE; // determine average by dividing by size of array
        
        // Add RSSI to data packet
        sprintf((char*)buf, "%s%d", buf, averageRSSI);

        // Encode message (halves the required memory)
        char* bleMsgEncoded = encodeBLE((char*)buf);

        // Send the encoded data over BLE
        bleuart.print(bleMsgEncoded);
        delete bleMsgEncoded;
      }
    }
  }      
}

void sendForcedRqst()
{
  // Send forced request LoRa message
  const uint8_t forcedReqBuf[] = "FRCD-RQST";
  uint8_t len = sizeof(forcedReqBuf);
  rf95.send(forcedReqBuf, len);
  delay(10);
  rf95.waitPacketSent(); // blocks until transmitter is no longer transmitting
  
  // Put transciever back in RX mode
  rf95.setModeIdle();
  delay(100);
  rf95.setModeRx();
  delay(100);
}

// BLE ENCODING
// 0-9 is 0001 to 1010
// ' ' is 1011
// '+' is 1100
// '-' is 1101
// nothing is 0000
const char& IGNORE_CHAR = 14;
char encodeLetter(const char& c) 
{
  if ('0' <= c && c <= '9') return (c - '0') + 1;
  if (c == ' ') return 11;
  if (c == '+') return 12;
  if (c == '-') return 13;
  Serial.println("failed encodeLetter " + String(c));
  return IGNORE_CHAR;
}

// Encodes a string (char array) to allow for double the bandwidth
char* encodeBLE(const char* s) 
{
  const int& len = strlen(s);
  // 5 letters becomes 3 letters + null pointer
  const int& out_len = ceil(len / 2.0) + 1;
  char* out = (char*)malloc(out_len * sizeof(char));
  for(int i = 0; i < len; i += 2) {
    char c = encodeLetter(s[i]) << 4; // shift to first 4 of 8
    if (i + 1 < len) {
      c |= encodeLetter(s[i+1]); // end to last 4 of 8
    } else {
      c |= IGNORE_CHAR;
    }
    out[i / 2] = c;
  }
  out[out_len - 1] = '\0';
  return out;
}

/////////////////////////////////////////////////////////////
//////////// HELPER FUNCTIONS ///////////////////////////////
/////////////////////////////////////////////////////////////

void setupBLE()
{
  // Setup BLE
  Bluefruit.begin();
  
  //TODO: Tune this value (maximize range, minimize power)
  Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values
  Bluefruit.setName("FindMe - Hub");

  // Callbacks for Peripheral
  Bluefruit.Periph.setConnectCallback(prph_connect_callback);
  Bluefruit.Periph.setDisconnectCallback(prph_disconnect_callback);
  
  // Start services
  bledfu.begin(); //To be consistent, OTA DFU should be added first if it exists
  bleuart.begin(); // Configure and start the BLE Uart service
  
  // Set up and start advertising
  startAdv();
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
    Sprintln("LoRa radio init failed");
    Sprintln("Uncomment '#define SERIAL_DEBUG' in RH_RF95.cpp for detailed debug info");
    while (1);
  }
  Sprintln("LoRa radio init OK!");
  
  // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
  if (!rf95.setFrequency(RF95_FREQ)) {
    Sprintln("setFrequency failed");
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

void startAdv(void)
{
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  
  // Include the BLE UART (AKA 'NUS') 128-bit UUID
  Bluefruit.Advertising.addService(bleuart);

  // Secondary Scan Response packet (optional)
  // Since there is no room for 'Name' in Advertising packet
  Bluefruit.ScanResponse.addName();

  /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until connected)
   * 
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html   
   */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds  
}
