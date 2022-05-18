# GSM_Relay
Arduino based remote relay which can be activated for a predefined time by unanswered (thus free) GSM voice call. 
Gate, light, boiler or any other device can be handled.

## Hardware parts:

- Arduino compatible device (e. g.  m328 based 5 V Arduino or breadboarded ATmega328p), 
- GSM module (AT commands compatible),
- at least 100 uF capacitor connected to  GSM module (GND, VCC) to prevent resetting the system on transmission peaks.

Take care about voltage (both devices has to use the same otherwise level convertor must be used for serial 
connection). 

The used parts are
- [Arduino Nano](http://store.arduino.cc/products/arduino-nano)
- [Air208](https://fccid.io/2AEGG-AIR208/User-Manual/User-Manual-3829903) GSM module



## Wiring 
Arduino compatible device uses two serial interfaces: 
1. is used for configuration, sketch uploading and debugging (hardware serial available via USB on Arduino Nano), 19200 Bd unless changed
2. is attached to the GSM module (SoftwareSerial on Arduino Nano is used, 9600 Bd)
 
Serial connection on Arduino side is attached to Arduino's m328 pins 2, 3 (you can change in sketch) and SoftwareSerial library is used. 



## Configuration
Before calling to the device, you have to specify at least two parameters using serial interface used for sketch uploading:

### authorised number(s) -  `REG` command

- register admins authorised phone number to eeprom 
`REG  +420xxxxxxxxx`

- register authorised phone number to SIM phonebook item of given *index* with given *contactName*

`REG <index> +420xxxxxxxxx <contactName>`

`REG 1 +42012345678 ItsMe`

`REG 2 +42000000000 ItsMyWife`

- list phonebook directory on SIM:
`REG` 
        

### how many ticks are added on call - `PERIOD` command

`PERID <numberOfTicks>`

Ticks are about 0.5 seconds unless changed in the sketch (LOOP_DELAY).


 ## Usage


- Calls from unauthorised numbers are immediately hang by GSM_Relay on remote side.


- To switch the remote relay ON for a time specified by `PERIOD`, make a voice call from authorised phone number to the GSM_Relay device and *do not hang*. 
After 4 rings, *the remote side will hang* (so you know your call was accepted) and relay switched on for a period (invoke `STATUS` command and see `relayTickCount` for remaining ticks).

- When you repeat your call while relay is ON, additional PERIOD is add to the remaining time (`relayTickCount`).

- To switch the relay OFF immediately, call GSM_Relay, wait for the first ring and hang up.







