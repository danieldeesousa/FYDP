#include <string.h>
#include <bluefruit.h>
#include <SPI.h>
#include <RH_RF95.h>

// LoRa pin definitions
#define RFM95_RST 30
#define RFM95_CS 27
#define RFM95_INT 31

// BLE services
BLEDfu bledfu; // OTA DFU service
BLEUart bleuart; // Uart over BLE service

// LoRa definitions
#define RF95_FREQ 915.0 // Can also be 434.0 - must match freq of chipset
RH_RF95 rf95(RFM95_CS, RFM95_INT); // Singleton instance of the radio driver
#define RSSI_BUF_SIZE 5
int16_t rssiBuf[RSSI_BUF_SIZE]; // used to average RSSI value
int8_t rssiBuf_idx = 0;
int16_t averageRSSI;

void setup(void)
{
  Serial.begin(115200);
  Serial.println("FindMe - Hub booted");
  
  // Demo: Red LED
  pinMode(LED_BUILTIN, OUTPUT);

  // Setup BLE
  Bluefruit.begin();
  //TODO: Tune this value (maximize range, minimize power)
  Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values
  Bluefruit.setName("FindMe - Hub");
  // Start services
  bledfu.begin(); //To be consistent, OTA DFU should be added first if it exists
  bleuart.begin(); // Configure and start the BLE Uart service
  // Set up and start advertising
  startAdv();

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
  Serial.print("Set Freq to: "); Serial.println(RF95_FREQ);
  // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on
  // The default transmitter power is 13dBm, using PA_BOOST.
  // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then
  // you can set transmitter powers from 5 to 23 dBm:
  rf95.setTxPower(23, true);
}

void loop(void)
{
  // Wait for LoRa message
  if (rf95.available())
  {
    // Should be a message for us now
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);;
    if (rf95.recv(buf, &len))
    {
      digitalWrite(LED_BUILTIN, HIGH);

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
      Serial.println((char*)buf);
      
      // Send this data over BLE
      bleuart.print((char*)buf);
      
      digitalWrite(LED_BUILTIN, LOW);
    }
  }      
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
