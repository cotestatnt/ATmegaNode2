/*
DebugUtils.h - Simple debugging utilities.
*/

#define SLP_1S B01000110                // 1 Second Timeout
#define SLP_2S B01000111                // 2 Second Timeout
#define SLP_4S B01100000                // 4 Second Timeout
#define SLP_8S B01100001                // 8 Second Timeout
#define SLP_FOREVER B11111111           

  
//#define DEBUG

#ifndef DEBUGUTILS_H
#define DEBUGUTILS_H

#ifdef DEBUG
  #define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTLN(...)
#endif

#endif
