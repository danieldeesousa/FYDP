#include <string.h>
#include <bluefruit.h>
#include <SPI.h>
#include <RH_RF95.h>

// Pin definitions
#define RFM95_RST 30
#define RFM95_CS 27
#define RFM95_INT 31
#define BATTERY_VOLTAGE (A0) // change dependent on PCB
#define TAG_CHARGE_DET (A1) // change dependent on PCB
#define WIFI_LED // driven on ESP8266
#define BLE_LED (19) // driven by low-level BLE lib
#define TAG_CHARGE_LED (15) // change dependent on PCB

// BLE services
BLEDfu bledfu; // OTA DFU service
BLEUart bleuart; // Uart over BLE service

// LoRa definitions
#define RF95_FREQ 915.0 // Can also be 434.0 - must match freq of chipset
RH_RF95 rf95(RFM95_CS, RFM95_INT); // Singleton instance of the radio driver

// Analog definitions
#define ADC_RES (1024.0) // default 10bit resolution - can be 8,10,12,14
#define REF_VOLTAGE (3.6) // Internal ADC reference voltage
#define VOLTAGE_USB (4.5) // Voltage detected when USB is in

// Application variables
#define RSSI_BUF_SIZE 1
int16_t rssiBuf[RSSI_BUF_SIZE]; // used to average RSSI value
int8_t rssiBuf_idx = 0;
int16_t averageRSSI;
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
  Serial.println("FindMe - Hub booted");
  
  // Demo LED
  pinMode(TAG_CHARGE_LED, OUTPUT);
  pinMode(BLE_LED, OUTPUT);

  setupBLE();
  setupLoRa();
}

void loop(void)
{
  // Check if tag is plugged in
  float tagChargeVoltage = (analogRead(TAG_CHARGE_DET) / ADC_RES) * REF_VOLTAGE;

  // TAG LED charging behaviour: flashing (charging), solid (charge complete)
  if(tagChargeVoltage > 1)
  {
    digitalWrite(TAG_CHARGE_LED, HIGH);
  }
  
  // Check LoRa
  recieveLoRaPacket();
  delay(1000);

  // TAG LED charging behaviour: flashing (charging), solid (charge complete)
  // TODO: check here is charge complete (dont turn off LED when charge complete)
  if(tagChargeVoltage > 1)
  {
    digitalWrite(TAG_CHARGE_LED, LOW);
  }

  // Check ESP Serial port
  readSerial();
  delay(1000); 


}

void readSerial()
{
  char input[20];
  input[0] = '\0';
  int idx = 0;

  if(Serial.available() > 0)
  {
    // append to array
    input[idx] = Serial.read();
    idx++;

    // check if end of message
    if(input[idx-1] == '\n')
    {
      input[idx-1] = '\0';
      if(strncmp(input, "FRCD-RQST", 9) == 0)
      {
        sendForcedRqst();
      }
    }
  }
}

void recieveLoRaPacket()
{
  digitalWrite(LED_BUILTIN, HIGH);
  
  // Wait for LoRa message
  if (rf95.available())
  {
    // Should be a message for us now
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);;
    if (rf95.recv(buf, &len))
    {
      // Terminate message
      buf[len] = '\0';
      
      // Print packet
      Serial.println("LoRa RX: ");
      Serial.println((char*)buf);

      // Determine whether to send over Wi-Fi
      // Check if USB is connected
      // *2 because of voltage divider
      float battVoltage = 2*(analogRead(BATTERY_VOLTAGE) / ADC_RES) * REF_VOLTAGE;
      if(battVoltage > VOLTAGE_USB)
      {
        // Send LoRa packet to ESP to process
        Serial.write((char*)buf);
      }

      // Determine whether to send over BLE
      if(isBLEConnected == true)
      {
        // Remove battery and alert info
        // Confirm with FYDP team
        buf[16] = '\0';
        
        // Get RSSI
        Serial.println(rf95.lastRssi());
        rssiBuf[rssiBuf_idx++] = rf95.lastRssi();
        rssiBuf_idx %= RSSI_BUF_SIZE;
    
        // Calcute average RSSI
        averageRSSI = 0; // reset
        for (int i = 0; i < RSSI_BUF_SIZE; i++){
           averageRSSI += rssiBuf[i]; // add up all RSSIs
        }
        averageRSSI /= RSSI_BUF_SIZE; // determine average by dividing by size of array
        
        // Add RSSI to data packet
        sprintf((char*)buf, "%s%d", buf, rf95.lastRssi());
        //Serial.println((char*)buf);
        
        // Send this data over BLE
        bleuart.print((char*)buf);
      }
    }
  }      

  digitalWrite(LED_BUILTIN, LOW);
}

void sendForcedRqst()
{
  digitalWrite(LED_BUILTIN, HIGH);

  // Enable transmitter
  rf95.setModeTx();

  // Place lat/long data into buffer to be sent over LoRa
  uint8_t buf[] = "FRCD-RQST";

  // Send LoRa message with GPS coordinates
  rf95.send(buf, 20);
  delay(10);
  rf95.waitPacketSent(); // blocks until transmitter is no longer transmitting
  
  // Put transciever back in RX mode
  rf95.setModeRx();
  
  digitalWrite(LED_BUILTIN, LOW);
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
    Serial.println("LoRa radio init failed");
    Serial.println("Uncomment '#define SERIAL_DEBUG' in RH_RF95.cpp for detailed debug info");
    digitalWrite(LED_BUILTIN, HIGH);
    while (1);
  }
  Serial.println("LoRa radio init OK!");
  
  // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
  if (!rf95.setFrequency(RF95_FREQ)) {
    Serial.println("setFrequency failed");
    digitalWrite(LED_BUILTIN, HIGH);
    while (1);
  }

  // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on
  // The default transmitter power is 13dBm, using PA_BOOST.
  // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then
  // you can set transmitter powers from 5 to 23 dBm:
  rf95.setTxPower(23, true);

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
