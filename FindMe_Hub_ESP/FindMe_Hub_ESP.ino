
void setup() {
  Serial.begin(9600);
  pinMode(0, OUTPUT);
}
 
void loop() {
  digitalWrite(0, HIGH);
  Serial.write("ON\n"); // Will transmit the value 65
  delay(1000);
  digitalWrite(0, LOW);
  Serial.write("OFF\n"); // Will transmit the value 65
  delay(1000);
}
/*

  static char input[20];
  static int idx = 0;
  
void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
}
 
void loop() 
{
  while(Serial.available() > 0)
  {
    // append to array
    input[idx] = Serial.read();
    idx++;

    // check if end of message
    if(input[idx-1] == '\n')
    {
      input[idx-1] = '\0';
      if(strcmp(input, "ON") == 0)
      {
        digitalWrite(LED_BUILTIN, HIGH);
      }
      if(strcmp(input, "OFF") == 0)
      {
        digitalWrite(LED_BUILTIN, LOW);
      }

      // reset array
      idx = 0;
      input[0] = '\0';
    }
  }
}*/
