
// In 
// ~/Library/Arduino15/packages/arduino/hardware/avr/1.8.5/libraries/SoftwareSerial/src/SoftwareSerial.h
// change RX buffer size to 255 bytes
// #define _SS_MAX_RX_BUFF 255
#include <SoftwareSerial.h>

//Create software serial object to communicate with SIM900
SoftwareSerial mySerial(2,3); // mySerial(7, 8); //SIM900 Tx & Rx is connected to Arduino #7 & #8


void setup()
{
  //Begin serial communication with Arduino and Arduino IDE (Serial Monitor)
  Serial.begin(9600);
  
  //Begin serial communication with Arduino and SIM900
  mySerial.begin(9600);

  Serial.println("Initializing..."); 

   mySerial.println("AT"); //Handshaking with SIM900
  updateSerial();
  mySerial.println("AT+IPR=9600"); // set baudrate
  updateSerial();
  mySerial.println("ATI"); //Handshaking with SIM900
  updateSerial();


  mySerial.println("AT+CMEE=2"); // enable verbode error code
  updateSerial();
  

  mySerial.println("AT+CSQ"); //Signal quality test, value range is 0-31 , 31 is the best
  updateSerial();
  mySerial.println("AT+CCID"); //Read SIM information to confirm whether the SIM is plugged
  updateSerial();
  mySerial.println("AT+CREG?"); //Check whether it has registered in the network
  updateSerial();
  
  mySerial.println("AT+COPS?"); 
  updateSerial();
  //mySerial.println("AT+COPS=?"); // Return the list of operators present in the network
  //updateSerial();

 Serial.println("Input AT commands:"); 
}

void loop()
{
  updateSerial();
}

void updateSerial()
{
  while (Serial.available()) 
  {
    mySerial.write(Serial.read());//Forward what Serial received to Software Serial Port
  }
   delay(1000);
  while(mySerial.available()) 
  {
    Serial.write(mySerial.read());//Forward what Software Serial received to Serial Port
  }
}
