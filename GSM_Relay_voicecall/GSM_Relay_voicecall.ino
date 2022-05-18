// # GSM_Relay
//  Arduino based remote relay which can be activated by unanswered (thus free) GSM voice call.
//  Gate, light, boiler or any other device can be handled.
//  ## Hardware parts:
//  - m328 based 5 V Arduino or breadboarded ATmega328p,
//  - GSM module (AT commands compatible, attached to m328 port 2,3)

// nano pinout:
//https://i.stack.imgur.com/pSe2G.png

// General AT commands:
// https://m2msupport.net/m2msupport/general-at-commands/

// Air720 series AT commands:
// https://www.openluat.com/Product/file/asr1802/AT%20COMMAND%20Set%20for%20Luat%204G%20Modules_V3.89.pdf


#include <SoftwareSerial.h>
#include <CmdParser.hpp> // https://github.com/pvizeli/CmdParser
#include <EEPROM.h>

// hardware pins
SoftwareSerial mySerial (2, 3) ;
#define PIN_RELAY 11 // use "RELAY TESTPIN" to find out

// glabal vars
#define LOOP_DELAY 500 // ms
String myNum = "604546116"; // hardcoded authorised number
String eepromNum = ""; // authorized phone number is stored in EEPROM and MUST BE SET by command REG
// all phone numbers stored in SIM phonebook under name beginning with "REG " are considered to be authorized


static unsigned long  loopTickCount = 0;
static  long  relayTickCount = 0; // <= 0  means OFF
static unsigned long relayAddTicksOnCall = 1 * 60 / 3; // relay on remaining ticks, one tick ~ 3 s

static int callCount = 0 ; // number of activating calls
uint8_t ringCount = 0 ;

static unsigned long lastCallTick = 0;

///////////////////////////////////////////////////////////////////

void EEPROMWritelong(int address, long value) {

  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);

  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
}

long EEPROMReadlong(long address) {
  long four = EEPROM.read(address);
  long three = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);

  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}


int writeStringToEEPROM(int addrOffset, const String &strToWrite)
{
  byte len = strToWrite.length();
  EEPROM.write(addrOffset, len);
  for (int i = 0; i < len; i++)
  {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }
  return addrOffset + len + 1;
}
String readStringFromEEPROM(int addrOffset)
{
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];
  for (int i = 0; i < newStrLen; i++)
  {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  data[newStrLen] = '\0';
  return String(data);
}

///////////////////////////////////////////////////////////////////

String myBufferStr = "" ;
CmdBuffer<255> myBuffer;
CmdParser     myParser;

void AT_handleResponse(uint32_t timeout = 1000) {
  // Read line and parse from GSM until timeout
  if (myBuffer.readFromSerial(&mySerial, timeout)) {
    myBufferStr = myBuffer.getStringFromBuffer();
    if (myParser.parseCmd(&myBuffer) != CMDPARSER_ERROR) {
      // RINGING, get  CLIP
      if (myParser.equalCommand_P(PSTR("+CLIP:"))) {
        digitalWrite(LED_BUILTIN, HIGH);
        Serial.print("*"); Serial.println(myBufferStr);
        //          for (int i = 0; i < myParser.getParamCount(); i++)  {
        //            Serial.print(" '"); Serial.print(myParser.getCmdParam(i)); Serial.println("'");
        //          }
        eventRing(myParser.getCmdParam(1), myParser.getCmdParam(4)); // number, name (from phonebook)
      } else if (myBufferStr.indexOf("NO CARRIER") != -1) {
        // calling side hanged up
        Serial.print(F("*")); Serial.println(myBufferStr);
        AT_hangup(F("no carrier"));
        EventShortRing(ringCount);
        ringCount = 0;
      } else {
        // unparsed result from GSM module - just print it
        Serial.print(F(":")); Serial.println(myBufferStr);
      }
    }
  }
}


void AT_cmd (String cmd, uint32_t d = 3000, uint32_t timeout = 2000) {
  Serial.println(">" + cmd);
  mySerial.println(cmd);
  delay(d);
  AT_handleResponse(timeout);
}

void AT_hangup(String msg) {
  ringCount = 0;
  lastCallTick = loopTickCount;
  Serial.print(F("*** HANG UP *** "));
  Serial.println(msg);
  AT_cmd(F("ATH"), 5000); // wait long,  maybe some late +CLIP will come
  //Serial.println(F(">ATH"));
  //mySerial.println(F("ATH"));
  digitalWrite(LED_BUILTIN, LOW);
  // TODO: test whether line is hung up
}


/*
  void Serial_handleInput(uint32_t timeout = 1000) {
  while (Serial.available())
  {
    //Forward commands coming from  Serial Port to Software Serial (ie to GSM module)
    String myBuffer = Serial.readString();
    //AT_cmd(myBuffer);
    Serial.print(F(">"));
    Serial.println(myBuffer);
    mySerial.println(myBuffer);
  }
  }
*/

void Serial_handleInput(uint32_t timeout = 1000)
{
  if (myBuffer.readFromSerial(&Serial, timeout)) {
    myBufferStr = myBuffer.getStringFromBuffer();
    if (myParser.parseCmd(&myBuffer) != CMDPARSER_ERROR) {
      if (myParser.equalCommand_P(PSTR("HELP"))) {
        Serial_printHelp();
      } else if (myParser.equalCommand_P(PSTR("S")) || myParser.equalCommand_P(PSTR("STATUS"))) {
        // STATUS
        Serial.print(F("millis since poweron: ")); Serial.println(millis());
        Serial.print(F("ticks since poweron: ")); Serial.println(loopTickCount);
        Serial.print(F("relay state is ")); Serial.println(getRelayState());
        Serial.print(F("relay on remaining ticks:  ")); Serial.println(relayTickCount);
        Serial.print(F("relay activating calls: ")); Serial.println(callCount);
        Serial.print(F("ringCount: ")); Serial.println(ringCount);
        Serial.print(F("lastCallTick: ")); Serial.println(lastCallTick);
        Serial.print(F("relayAddTicksOnCall: ")); Serial.println(relayAddTicksOnCall);
      }  else if (myParser.equalCommand_P(PSTR("REG"))) {
        // REGister
        if (myParser.getParamCount() == 2) { // register to EEPROM
          eepromNum = myParser.getCmdParam(1);
          writeStringToEEPROM(sizeof(relayAddTicksOnCall), eepromNum); // the first data in EEPROM is long relayAddTicksOnCall
        } else if (myParser.getParamCount() == 4) { // register to SIM phonebook
          String index = myParser.getCmdParam(1);
          String phoneNum = myParser.getCmdParam(2);
          String contactName = myParser.getCmdParam(3);
          Serial.print(F("REGister to phonebook index "));
          Serial.print(index);
          Serial.print(F(": "));
          Serial.println(phoneNum);
          AT_cmd("AT+CPBW=" + index +   ",\"" + phoneNum + "\",145,\"REG " + contactName + "\"");
        } else if (myParser.getParamCount() == 1) {
          // nothing to do, just print curent state (bellow)
        } else {
          Serial.print(F("REG syntax error, ignored, invalid number of parameters "));
          Serial.print(myParser.getParamCount());
          Serial.print(F(". "));
          Serial.println(myBufferStr);
          Serial_printHelp();
        }
        // print current state
        Serial.println("hardcoded REGistered number: " + myNum);
        Serial.println("eeprom REGistered number: " + eepromNum);
        AT_cmd(F("AT+CPBR=1,99")); // list whole directory to see changes
      }  else if (myParser.equalCommand_P(PSTR("SMS"))) {
        // SMS
        AT_cmd(F("AT+CMGF=1"));
        AT_cmd(F("AT+CPMS=\"SM\""));
        AT_cmd(F("AT+CMGL=\"ALL\""));
        AT_cmd(F("AT+CPMS=\"ME\""));
        AT_cmd(F("AT+CMGL=\"ALL\""));
      } else if (myParser.equalCommand_P(PSTR("RELAY"))) {
        // RELAY
        if (myParser.getParamCount() == 1) {
          Serial.print(F("Relay is "));  Serial.println(getRelayState());
        } else {
          String s1 = myParser.getCmdParam(1);
          if (s1 == "ON" or s1 == "1") {
            setRelayState(true);
          } else if (s1 == "OFF" or s1 == "0") {
            setRelayState(false);
          } else if (s1 == "TESTPIN" ) { // test all output pins
            for (int i = 4; i < 32; i++) {
              Serial.println(i);
              pinMode(i, OUTPUT);
              digitalWrite(i, HIGH);
              delay(2000);
            }
          }
          else {
            Serial.println("RELAY syntax error in '"  + s1 + "', " + myBufferStr);
          }
        }
      } else if (myParser.equalCommand_P(PSTR("PERIOD"))) {
        // PERIOD
        if (myParser.getParamCount() == 1) {
          Serial.print(F("relayAddTicksOnCall is  ")); Serial.println(relayAddTicksOnCall);
        }  else if (myParser.getParamCount() == 2) {
          long p = atol(myParser.getCmdParam(1));
          Serial.print(F("Setting relayAddTicksOnCall to ")); Serial.println(p);
          relayAddTicksOnCall = p;
          EEPROMWritelong(0, p);
        } else {
          Serial.println("PERIOD syntax error, wrong number of parameters " + myParser.getParamCount());
        }
      }
      else
      // AT - undetected input send directly to GSM module (probably AT command)
      {
        AT_cmd(myBufferStr);
      }
    }
    else { // CMDPARSER_ERROR
      Serial.print(F("Serial In Parse error. >"));
      Serial.println(myBufferStr);
      AT_cmd(myBufferStr);
    }
  }
}

void Serial_printHelp() {
  // HELP
  Serial.println(F("REG .. list phonebook directory on SIM"));
  Serial.println(F("REG  +420xxxxxxxxx .. register admins authorized phone number to eeprom "));
  Serial.println(F("REG index +420xxxxxxxxx contactName .. register authorized phone number to SIM phonebook item of given index with given contactName"));
  Serial.println(F("S[TATUS] .. get status, times, ticks, ..."));
  Serial.println(F("RELAY [0|1]"));
  Serial.println(F("PERIOD [valueInTicks] .. get/set how many ticks are added on call" ));
  Serial.println(F("SMS .. list sms"));
  Serial.println(myParser.getParamCount() );
}


void eventRing(String callNum, String callName) {
  ringCount++;
  if (callNum == myNum || callNum == eepromNum || callName.startsWith("REG ")) {
    Serial.print(F("*ringing ")); Serial.print(ringCount); Serial.print(F("x "));
    Serial.print(callNum);
    Serial.print(F(" '")); Serial.print(callName); Serial.println(F("'"));
    if (lastCallTick + 10 > loopTickCount) {
      Serial.println(F("ignoring early ring") );
      AT_hangup(F("ignoring early ring"));
    } else if (ringCount > 3) {
      AT_hangup(F("handling long ring"));
      EventLongRing();
    }
  }
  else {
    AT_hangup("ignoring unathorized caller " + callNum + " " + callName);
  }
}



/////////////////////////////////////////////////////////////////////////////////////

static bool relayState;

void setRelayState (bool r) {
  if (r == true) {
    relayState = 1;
    Serial.println(F("Relay ON"));
    digitalWrite(PIN_RELAY, HIGH);
  } else if (r == false) {
    relayState = 0 ;
    Serial.println(F("Relay OFF"));
    digitalWrite(PIN_RELAY, LOW);
  } else {
    Serial.println(F("ERROR, setRelayState must have bool arg!"));
  }
}

bool getRelayState () {
  return relayState;
}


void resetRelay() {
  callCount = 0;
  relayTickCount = 0;
  setRelayState(0) ;
  Serial.println(F("*** Relay off and reset."));
}

void EventShortRing (uint8_t rings) {
  Serial.println(F("*** Short ring, resetting relay."));
  resetRelay();
}

void EventLongRing () {
  callCount++;
  relayTickCount += relayAddTicksOnCall;
  setRelayState(1);
}



void setup()
{
  //Begin serial communication with Arduino and Arduino IDE (Serial Monitor)
  Serial.begin(19200);

  //Begin serial communication with Arduino and SIM900
  mySerial.begin(9600);

  Serial.print(F("Testing relay..."));

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(PIN_RELAY, OUTPUT);
  setRelayState(1);
  delay(200);
  setRelayState(0);

  Serial.println(F("Initializing..."));
  // read config from eeprom
  relayAddTicksOnCall = EEPROMReadlong(0);
  eepromNum = readStringFromEEPROM(sizeof(relayAddTicksOnCall)); // the first data in EEPROM is long relayAddTicksOnCall
  
  Serial.println("eepromNum=" + eepromNum + ", myNum=" + myNum + ", relayAddTicksOnCall=" + relayAddTicksOnCall);


  delay(5000);


  AT_cmd(F("AT")); //Handshaking with SIM900
  AT_cmd(F("AT+IPR=9600")); // set baudrate
  AT_cmd(F("ATI")); //Handshaking with SIM900
  AT_cmd(F("AT+CMEE=2")); // enable verbode error code
  //AT_cmd(F("AT+CGMI"));  //Request manufacturer information
  //AT_cmd(F("AT+CGMM"));  //Request model identification
  //AT_cmd(F("AT+CGMR");  //Request revision identification
  AT_cmd(F("AT+CSQ")); //Signal quality test, value range is 0-31 , 31 is the best
  AT_cmd(F("AT+CCID")); //Read SIM information to confirm whether the SIM is plugged
  //AT_cmd(F("AT+CREG?")); //Check whether it has registered in the network
  //AT_cmd(F("AT+CIMI")); // Request international mobile subscriber identity
  //AT_cmd(F("AT+CGSN")); //Request product serial number identification (of the device, not SIM card)
  AT_cmd(F("AT+COPS?")); // Check that you’re connected to the network
  //AT_cmd(F("AT+COPS=?"), 5000); // Return the list of operators present in the network
  Serial.println(F("Ready. Use following config/debug commands or issue AT command to your serial console"));
  Serial_printHelp();
}

void updateSerial(uint32_t timeout = 1000)
{
  //Forward what Serial received to GSM Software Serial Port
  while (Serial.available())
    Serial_handleInput(timeout);
  delay(LOOP_DELAY);
  // Handle output from GSM module (and print it to Serial)
  AT_handleResponse(timeout);
}

void loop()
{
  if (loopTickCount++ % 10 == 0) {
    Serial.print(".");
  };

  if (relayTickCount > 1) {
    relayTickCount--;
  }
  else if (relayTickCount == 1) {
    resetRelay();
  }

  updateSerial();
}
