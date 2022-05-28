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


// In
// ~/Library/Arduino15/packages/arduino/hardware/avr/1.8.5/libraries/SoftwareSerial/src/SoftwareSerial.h
// enlarge RX buffer size by
// #define _SS_MAX_RX_BUFF 255 // SoftwareSerial RX buffer size
#include <SoftwareSerial.h>

#include <CmdParser.hpp> // https://github.com/pvizeli/CmdParser

#include <EEPROM.h>

// hardware pins
SoftwareSerial gsmSerial (2, 3) ;
#define PIN_RELAY 11 // use "RELAY TESTPIN" to find out correct pin number
#define LED LED_BUILTIN

// glabal vars
#define LOOP_DELAY 500 // ms

// authorisation global vars
// SIM phone directory is main storage of authorised numbers
// all phone numbers stored in SIM phonebook under name beginning with "REG " are considered to be authorized
// use "REG index number contactName" command to add/change authorized numbers in SIM
static const String myNum = "604546116"; // hardcoded admin's authorised number
static String eepromNum; // configurable admin's authorized phone number stored in EEPROM --- use command REG to set it


static unsigned long  loopTickCount = 0;
static uint32_t  relayStopTime = 0; // when relay will be switched off (millis() is the system time), 0 means relay is already OFF
static uint32_t relayAddTimeOnCall; // relay on interval (miliseconds), stored in EEPROM -- use command PERIOD to set it
#define maxVoiceCalls 4 // maximal number of calls which adds relayAddTimeOnCall

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
/////////////////////////////////////////////////////////////////
void msToHMS( const uint32_t ms, uint16_t &h, uint8_t &m, uint8_t &s )
{
  uint32_t t = floor(ms / 1000);
  s = t % 60;
  t = (t - s) / 60;
  m = t % 60;
  t = (t - m) / 60;
  h = t;
}
void printHMS( const uint32_t ms )
{
  uint16_t h;
  uint8_t m;
  uint8_t s;
  msToHMS(ms, h, m , s);
  Serial.print(h);
  Serial.print(F(":"));
  Serial.print(m);
  Serial.print(F(":"));
  Serial.print(s);
}



///////////////////////////////////////////////////////////////////

String myBufferStr = "" ;
CmdBuffer<255> myBuffer;
CmdParser     myParser;

void AT_handleResponse(uint32_t timeout = 5000) {
  // Read line and parse from GSM until timeout
  myBuffer.clear();
  while (myBuffer.readFromSerial(&gsmSerial, timeout)) {
    myBufferStr = myBuffer.getStringFromBuffer();
    if (myParser.parseCmd(&myBuffer) != CMDPARSER_ERROR) {
      /// CLIP = RING
      if (myParser.equalCommand_P(PSTR("+CLIP:"))) {
        // RINGING, parse caller number and name from CLIP
        String callNum = myParser.getCmdParam(1);
        String callName = myParser.getCmdParam(4);
        ringCount++;

        digitalWrite(LED, HIGH);
        Serial.print("*"); Serial.println(myBufferStr);
        //          for (int i = 0; i < myParser.getParamCount(); i++)  {
        //            Serial.print(" '"); Serial.print(myParser.getCmdParam(i)); Serial.println("'");
        //          }

        if (callNum == myNum || callNum == eepromNum || callName.startsWith("REG ")) {
          // caller is authorised
          Serial.print(F("*ringing ")); Serial.print(ringCount); Serial.print(F("x "));
          Serial.print(callNum);
          Serial.print(F(" '")); Serial.print(callName); Serial.println(F("'"));
          if (lastCallTick + 10 > loopTickCount) {
            // EARLY ring
            Serial.println(F("ignoring early ring") );
            AT_hangup(F("ignoring early ring"));
          } else if ( ((ringCount > 3) && (callCount + 1 <= maxVoiceCalls)) || (ringCount > 8)) {
            // LONG ring
            callCount++;
            if (callCount <= maxVoiceCalls) {
              AT_hangup(F("handling long ring"));
              addRelayTime(1);
            } else {
              AT_hangup(F("maxVoiceCalls reached, ignoring long ring"));
            }
          }
        } else {
          // unathorized caller
          AT_hangup("ignoring unathorized caller " + callNum + " " + callName);
        }
      } else if (myBufferStr.indexOf("NO CARRIER") != -1) {
        /// remote party hang up
        Serial.print(F("*")); Serial.println(myBufferStr);
        AT_hangup(F("no carrier"));
        Serial.println(F("*** Short ring, resetting relay."));
        resetRelay();
        ringCount = 0;
      } else {
        /// unparsed result from GSM module - just print it
        Serial.print(F(":")); Serial.println(myBufferStr);
      }
    } else {
      // parsing of GSM response failed, print it (ignoring empty lines)
      if (myBufferStr.length() > 0) {
        Serial.print(F("?"));
        Serial.println(myBufferStr);
      }
    }
  }
}


void AT_cmd (String cmd, uint32_t d = 4000, uint32_t timeout = 5000) {
  Serial.println(">" + cmd);
  gsmSerial.print(cmd);
  gsmSerial.println(F("\r"));
  delay(d);
  AT_handleResponse(timeout);
}

void AT_hangup(String msg) {
  ringCount = 0;
  lastCallTick = loopTickCount;
  Serial.print(F("*** HANG UP *** "));
  Serial.println(msg);
  digitalWrite(LED, LOW);
  AT_cmd(F("ATH"), 5000); // wait long,  maybe some late +CLIP will come and hang up before relay will turn on (maybe not enough power)
  // TODO: test whether line is hung up (wait for "OK" ???)
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
    gsmSerial.println(myBuffer);
  }
  }
*/

void Serial_handleInput(uint32_t timeout = 1000)
{
  myBuffer.clear();
  if (myBuffer.readFromSerial(&Serial, timeout)) {
    myBufferStr = myBuffer.getStringFromBuffer();
    if (myParser.parseCmd(&myBuffer) != CMDPARSER_ERROR) {
      if (myParser.equalCommand_P(PSTR("HELP"))) {
        Serial_printHelp();
      } else if (myParser.equalCommand_P(PSTR("S")) || myParser.equalCommand_P(PSTR("STATUS"))) {
        // STATUS
        Serial.print(F("time since poweron: "));
        Serial.print(floor(millis() / 1000));
        Serial.print(F(" [s] = "));
        printHMS(millis());
        Serial.println(F(" [h:m:s]"));
        Serial.print(F("ticks since poweron: ")); Serial.println(loopTickCount);
        Serial.print(F("relay state is ")); Serial.println(getRelayState());
        if (relayStopTime != 0)  {
          Serial.print(F("relay on remaining time:  "));
          printRelayRemainingTime();
          Serial.print(F("relay will stop at [s]:  ")); Serial.println(floor(relayStopTime) / 1000);
        }
        Serial.print(F("ringCount: ")); Serial.println(ringCount);
        Serial.print(F("lastCallTick: ")); Serial.println(lastCallTick);
        Serial.print(F("relayAddTimeOnCall [s]: ")); Serial.println(relayAddTimeOnCall / 1000);
        Serial.print(F("relay activating calls (callCount): ")); Serial.println(callCount);
        Serial.print(F("maxVoiceCalls: ")); Serial.println(maxVoiceCalls);
      }  else if (myParser.equalCommand_P(PSTR("REG"))) {
        // REGister
        if (myParser.getParamCount() == 2) { // register to EEPROM
          eepromNum = myParser.getCmdParam(1);
          writeStringToEEPROM(sizeof(relayAddTimeOnCall), eepromNum); // the first data in EEPROM is long relayAddTimeOnCall
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
          if (myParser.equalCmdParam_P(1, PSTR("ON")) or myParser.equalCmdParam_P(1, PSTR("1"))) {
            setRelayState(1);
          } else if (myParser.equalCmdParam_P(1, PSTR("OFF")) or myParser.equalCmdParam_P(1, PSTR("0"))) {
            setRelayState(0);
          } else if (myParser.equalCmdParam_P(1, PSTR("ADD")) or myParser.equalCmdParam_P(1, PSTR("+"))) {
            addRelayTime(1);
          } else if (myParser.equalCmdParam_P(1, PSTR("TESTPIN"))) { // test all output pins
            for (int i = 4; i < 32; i++) {
              Serial.println(i);
              pinMode(i, OUTPUT);
              digitalWrite(i, HIGH);
              delay(2000);
            }
          }
          else {
            Serial.print(F("RELAY syntax error in '"));
            Serial.print(myParser.getCmdParam(1));
            Serial.print(F("', "));
            Serial.println(myBufferStr);
          }
        }
      } else if (myParser.equalCommand_P(PSTR("PERIOD"))) {
        // PERIOD
        if (myParser.getParamCount() == 1) {
          Serial.print(F("relayAddTimeOnCall [s] is  ")); Serial.println(relayAddTimeOnCall / 1000);
        }  else if (myParser.getParamCount() == 2) {
          uint32_t p = atol(myParser.getCmdParam(1)) * 1000;
          Serial.print(F("Setting relayAddTicksOnCall [ms] to ")); Serial.println(p);
          relayAddTimeOnCall = p;
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
      Serial.print(F("Serial input Parse error in >>"));
      Serial.println(myBufferStr);
      // AT_cmd(myBufferStr);
    }
  }
}

void Serial_printHelp() {
  // HELP
  Serial.println(F("REG .. list phonebook directory on SIM"));
  Serial.println(F("REG  +420xxxxxxxxx .. register admins authorized phone number to eeprom "));
  Serial.println(F("REG index +420xxxxxxxxx contactName .. register authorized phone number to SIM phonebook item of given index with given contactName"));
  Serial.println(F("S[TATUS] .. get status, times, ticks, ..."));
  Serial.println(F("RELAY [0|1|+]"));
  Serial.println(F("PERIOD [s] .. get/set how many time are added on single call, in seconds" ));
  Serial.println(F("SMS .. list sms"));
  Serial.println(myParser.getParamCount() );
}

/////////////////////////////////////////////////////////////////////////////////////

void setRelayState(bool s) {
  if (s) {
    Serial.print(F("Relay ON and stops in "));
    printRelayRemainingTime();
    digitalWrite(PIN_RELAY, HIGH);
  } else {
    relayStopTime = 0;
    Serial.println(F("Relay OFF"));
    digitalWrite(PIN_RELAY, LOW);
  }
}

void addRelayTime (uint8_t i) {
  if (i > 0) {
    if (relayStopTime == 0) {
      relayStopTime = millis() + i * relayAddTimeOnCall;
    } else {
      relayStopTime += i * relayAddTimeOnCall;
    }
    setRelayState(true);
  } else { // adding 0 means reset
    setRelayState(false);
  }
}

void printRelayRemainingTime() {
  if (relayStopTime == 0) {
    Serial.println(F("never."));
  } else {
    uint32_t t = relayStopTime - millis();
    Serial.print(floor(t / 1000));
    Serial.print(F(" [s] = "));
    printHMS(t);
    Serial.println(F(" [h:m:s]"));
  }
}

bool getRelayState () {
  return relayStopTime > 0;
}


void resetRelay() {
  callCount = 0;
  setRelayState(false) ;
  Serial.println(F("*** Relay off and reset."));
}


/////////////////////////////////////////////////////////////////////////////////////

void setup() {
  // Begin serial communication with Arduino and terminal (Serial Monitor in Arduino IDE on developer's computer)
  Serial.begin(19200);
  Serial.println(F(__FILE__));
  Serial.print(F("as at "));
  Serial.print(F(__DATE__));
  Serial.print(F(", "));
  Serial.println(F(__TIME__));
  Serial.println(F("V 1.1.0. Initializing..."));

  // buildin LED
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  // read config from eeprom
  relayAddTimeOnCall = EEPROMReadlong(0);
  eepromNum = readStringFromEEPROM(sizeof(relayAddTimeOnCall)); // the first data in EEPROM is long relayAddTicksOnCall
  Serial.println("eepromNum=" + eepromNum + ", myNum=" + myNum + ", relayAddTimeOnCall[s]=" + relayAddTimeOnCall / 1000);

  // relay
  pinMode(PIN_RELAY, OUTPUT);
  Serial.print(F("Testing relay..."));
  setRelayState(true);
  delay(200);
  setRelayState(false);

  // GSM module
  gsmSerial.begin(9600);
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

  // print some help
  Serial_printHelp();
  Serial.println(F("Ready. Use above commands or issue AT command to terminal"));
}

void updateSerial(uint32_t timeout = 1000) {
  // Forward what Serial received from your terminal,
  // parse commands and imnterpret them
  // and consider unparsed text as AT GSM commands (redirect them to gsmSerial).
  if (Serial.available()) {
    Serial_handleInput(timeout);
  } else {
    delay(LOOP_DELAY);
  }
  // Handle output from GSM module (print it to terminal and look for rings)
  AT_handleResponse(timeout);
}

void loop() {
  if (loopTickCount++ % 10 == 0) {
    // print we are alive to terminal
    Serial.print(getRelayState() == 0 ? F(".") : F(","));
  };
  if (ringCount == 0 && loopTickCount % 100 == 0) {
    // time to time let know to GSM module we are here (but not when ringing)
    // in the case of unexpected restart of GSM module (e. g. power issue), it will reestablish serial connection
    AT_cmd(F("AT"));
  };

  if (relayStopTime != 0 && millis() > relayStopTime)  {
    resetRelay();
  }

  updateSerial();
}
