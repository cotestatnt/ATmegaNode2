#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>
#include <CBC.h>
#include <AES.h>
#include <avr/wdt.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include "debug.h"

#define TIMEOUT		1000
#define wakeUpPin	2 
#define ADDR0		  4
#define ADDR1		  5
#define ADDR2		  6

void getRadioData(void);
bool sendRadioData();
uint16_t readVcc();
void sleepNow(byte sleepTime = SLP_8S);
void watchdogEnable(byte sleepTime = SLP_8S);       
void wakeUp();
void printHex(byte *buffer, byte bufferSize);

// Instantiate a AES block ciphering
#define BLOCK_SIZE 16
CBC<AES128> myChiper;
byte encryptKey[BLOCK_SIZE] = {0x5C, 0x39, 0x38, 0x7B, 0x36, 0x60, 0x68, 0x23, 0x34, 0x34, 0x76, 0x5C, 0x34, 0x22, 0x62, 0x55};
byte iv[BLOCK_SIZE] = {0x5C, 0x39, 0x38, 0x7B, 0x36, 0x60, 0x68, 0x23, 0x34, 0x34, 0x76, 0x5C, 0x34, 0x22, 0x62, 0x55};
byte cipherText[2*BLOCK_SIZE];
byte payload[2*BLOCK_SIZE];

enum States { SYS_DISABLED=0, SYS_ENABLED=10, SYS_ALIVE=20, DONE=40,  ALARMED=99 } systemStatus;
enum payloadPointer { _NodeH = 0, _NodeL = 1, _Enabled = 2, _MsgType = 3, _Battery = 4, _DeviceNumber = 6};
enum messageType { SET_DISABLE = 100, SET_ENABLE = 110, SEND_ALIVE = 120, SEND_REPLY = 130, GET_REQUEST = 140, SEND_ALARM = 199 };

unsigned long TimeStamp = 9999;
unsigned long RxTimeout = 0;

RF24 radio(10,9);                   // nRF24L01(+) radio attached using Getting Started board 
RF24Network network(radio);         // Network uses that radio
const uint16_t master_node = 00;    // Address of the other node in Octal format
uint16_t this_node = 01;            // Address of our node in Octal format ( 04,031, etc)
unsigned long t1, t2;
uint16_t device_number, battery = 0;

bool Alarmed = false;


// ***************************************************************************************************** //
// *****************************************    SETUP   ************************************************ //
// ***************************************************************************************************** // 
void setup(){	    
  analogReference(INTERNAL);    
  pinMode(wakeUpPin, INPUT_PULLUP); 
  attachInterrupt(digitalPinToInterrupt(wakeUpPin), wakeUp, FALLING);
  pinMode(ADDR0, INPUT_PULLUP);
  pinMode(ADDR1, INPUT_PULLUP);
  pinMode(ADDR2, INPUT_PULLUP);
  Serial.begin(115200);
	Serial.println();	  
  
  uint16_t setted_node = 1;    
  if(!digitalRead(ADDR0)) bitSet(setted_node, 0);
  if(!digitalRead(ADDR1)) bitSet(setted_node, 1);
  if(!digitalRead(ADDR2)) bitSet(setted_node, 2); 
  // Workaround octal var
  if(this_node != setted_node)
    this_node = setted_node;  
	DEBUG_PRINT(F("Node address: ")); 
	DEBUG_PRINTLN(this_node);

  payload[_NodeH] = (byte)((this_node >> 8) & 0xff);
  payload[_NodeL] = (byte)(this_node & 0xff);
  payload[_Battery] = readVcc(); 
   
  String now_time = __TIME__;
  now_time.replace(":", "");
  device_number = now_time.toInt();
  DEBUG_PRINT(F("Serial number: ")); 
  DEBUG_PRINTLN(device_number);

	// Init nRF24 Network 
  SPI.begin();
  radio.begin();
  radio.setRetries(3, 4);  
  radio.setDataRate(RF24_250KBPS);
  network.begin(/*channel*/ 90, /*node address*/ this_node);

  // Set encryption key  
  myChiper.setKey(encryptKey, BLOCK_SIZE);
  // Generate a random initialization vector and set it
  for (int i = 0 ; i < BLOCK_SIZE ; i++ ) 
    iv[i]= random(0xFF);
  myChiper.setIV(iv, BLOCK_SIZE);
  
	systemStatus = SYS_ENABLED;  
}



// ***************************************************************************************************** //
// *****************************************    LOOP   ************************************************* //
// ***************************************************************************************************** //

void loop(){  
  // Get data from nRF24L01+
  getRadioData();

  if (Alarmed)
    systemStatus = ALARMED;
    
  switch(systemStatus){
    // if system is disabled or enabled, simply set the new status byte (the master will take care about it)
    case SYS_DISABLED:
	      payload[_Enabled] = 0;	  
        systemStatus = SYS_ALIVE;	  
        break;
    case SYS_ENABLED:
	      payload[_Enabled] = 1;	  
	      systemStatus = SYS_ALIVE;	  
        break;
    case SYS_ALIVE:      		
        payload[_MsgType] = SEND_ALIVE;   
        payload[_NodeH] = (byte)((this_node >> 8) & 0xff);
        payload[_NodeL] = (byte)(this_node & 0xff);
        payload[_DeviceNumber + 0] = (byte)((device_number >> 8) & 0xff);
        payload[_DeviceNumber + 1] = (byte)(device_number & 0xff);    
        battery = readVcc();   
        payload[_Battery + 0] = (byte)((battery >> 8) & 0xff);                
        payload[_Battery + 1] = (byte)(battery & 0xff);          

        DEBUG_PRINT(F("Battery mV: "));
        DEBUG_PRINTLN(battery);
        DEBUG_PRINT(F("Send ALIVE: "));
        // Micro has resumed from deep sleep, send "alive" message to Master        

        if( sendRadioData()) {
          DEBUG_PRINT(F(" ..OK"));    
          systemStatus = DONE;
        }
        else {    
          DEBUG_PRINT(F(" ..Fail"));    
          #ifdef DEBUG
            delay(5);
          #endif                              
          sleepNow(SLP_1S) ;                
          systemStatus = SYS_ENABLED;
        }         
        break;	  

    // do nothing
    case DONE:
        DEBUG_PRINT(F("; Done (ms):"));
        if (battery < 3100) {    
          DEBUG_PRINTLN(F("\nLow battery"));   
          delay(10);                          
          sleepNow(SLP_FOREVER) ;                                  
        }
        else {
          #ifdef DEBUG                 
            delay(5); 
            t2 = micros() - t1;          
            Serial.println(t2);  
            delay(1);
          #endif     
          
          sleepNow(SLP_8S) ;                
          //////// Micro will be resumed here /////////                    
          
          #ifdef DEBUG                 
            t1 =  micros();
          #endif 
        }
      
        // Micro will be resumed from sleep at this point        
        systemStatus = SYS_ALIVE;   
        break;

    // This state is setted from uinterrupt on pin change
    // Send at least 3 message to master
    case ALARMED:    
        static byte cont = 0;
  		  payload[_MsgType] = SEND_ALARM;
  		  DEBUG_PRINT(F("\nALARM: "));
    		if( sendRadioData()) {
          DEBUG_PRINT(F(" ..OK"));           
          delay(100);          
          cont++;
          if(cont == 3){
            systemStatus = DONE;
            Alarmed = false;
            cont = 0;
          }
          else
            sleepNow(SLP_2S) ;  
        }
        else {    
          DEBUG_PRINT(F(" ..Fail"));    
          #ifdef DEBUG
            delay(10);
          #endif                              
          sleepNow(SLP_1S) ;                          
        }    
        break;
  }
}


// ***************************************************************************************************** //
// *****************************************    RF24   ************************************************* //
// ***************************************************************************************************** //
void getRadioData(void) {  
  network.update();  
  while (network.available()) {    
    RF24NetworkHeader Header;
    network.peek(Header);    
    if (Header.to_node == this_node) {
      network.read(Header, &cipherText, sizeof(cipherText));
        
      //*************** DECRYPT **********************//
      myChiper.decrypt(cipherText, payload, BLOCK_SIZE);
      //*************** DECRYPT **********************//
      
      #ifdef DEBUG
        DEBUG_PRINT(F("\nMaster:"));
        printHex(payload, 9);
      #endif      
    }
  }  
}

byte fast_random(){
  static byte a, b, c, x;
  x++;               //x is incremented every round and is not affected by any other variable
  a = (a^c^x);       //note the mix of addition and XOR
  b = (b+a);         //And the use of very few instructions
  c = (c+(b>>1)^a);  //the right shift is to ensure that high-order bits from b can affect  
  return c;          //low order bits of other variables
}

bool sendRadioData() {
  bool TxOK = false;
  
  for(byte i = 0; i <BLOCK_SIZE; i++){
     iv[i] = fast_random(); //random(255); 
     cipherText[i+BLOCK_SIZE] = iv[i];
  }
  #ifdef DEBUG
    printHex(payload, BLOCK_SIZE);
  #endif  
  //****************** ENCRYPT CBC AES 128  **********************//
  myChiper.setIV(iv, BLOCK_SIZE);
  myChiper.encrypt(cipherText, payload, BLOCK_SIZE);  
  //********************* END ENCRYPT ***************************//  
  #ifdef DEBUG
    DEBUG_PRINT("\nChiper:     ");
    printHex(cipherText, BLOCK_SIZE );
  #endif 

  RF24NetworkHeader header(/*to node*/ master_node);
  if (network.write(header, &cipherText, sizeof(cipherText)))
    TxOK = true; 

  return TxOK;
  
}



uint16_t readVcc() {
  uint16_t result;
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  //delayMicroseconds(50);  // Wait for Vref to settle
  ADCSRA |= _BV(ADSC);    // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = 1126400L / result; // Back-calculate AVcc in mV
  return result;
}


void sleepNow(byte sleepTime = SLP_8S){ 
  radio.powerDown();   
  wdt_reset();                         // Get ready to go to sleep...    
  ADCSRA &= ~(1 << ADEN);              // Turn OFF ADC module

  if (sleepTime != SLP_FOREVER)
    watchdogEnable(sleepTime);         // Turn on the watchdog timer   
      
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // Set sleep mode
  sleep_enable();                      // Enables the sleep bit in the mcucr register
  sleep_mode();                        // Here the device is actually put to sleep!!
  
  // THE PROGRAM CONTINUES FROM HERE AFTER WAKING UP
  sleep_disable();                     // Disable sleep after waking from. 
  ADCSRA |= (1 << ADEN);               // Turn ON ADC module
  radio.powerUp();                                     
}

 // Turn on watchdog timer; interrupt mode every 8.0s
void watchdogEnable(byte sleepTime = SLP_8S) {               
  cli();
  MCUSR = 0;
  WDTCSR |= B00011000;    
  WDTCSR = sleepTime;                     // 8 Second Timeout
  sei();  
} 

// WDT Wakeup
ISR (WDT_vect) {                      
  cli();
  wdt_disable();
  sei();
}
  
// Interrupt service routine for when button pressed
void wakeUp(){  
  if ((systemStatus > 0)) {
    #ifdef DEBUG
      if(!Alarmed)
        DEBUG_PRINTLN(F("Interrupt fired!"));      
    #endif     
    Alarmed = true;
  }
} 


// Helper routine to dump a byte array as hex values to Serial.
void printHex(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}
