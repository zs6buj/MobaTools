#ifndef MOTOSERVO_H
#define MOTOSERVO_H
/*
  MobaTools.h - a library for model railroaders
  Author: fpm, fpm@mnet-mail.de
  Copyright (c) 2020 All right reserved.

  Definitions and declarations for the servo part of MobaTools
*/

// defines for servos
#define Servo2	MoToServo		// Kompatibilität zu Version 01 und 02
#define OVLMARGIN           280     // Overlap margin ( Overlap is MINPULSEWIDTH - OVLMARGIN )
#define OVL_TICS       ( ( MINPULSEWIDTH - OVLMARGIN ) * TICS_PER_MICROSECOND )
#define MARGINTICS      ( OVLMARGIN * TICS_PER_MICROSECOND )
#define MAX_SERVOS  16  

#define MINPULSETICS    (MINPULSEWIDTH * TICS_PER_MICROSECOND)
#define MAXPULSETICS    (MAXPULSEWIDTH * TICS_PER_MICROSECOND)

#define OFF_COUNT       50  // if autoOff is set, a pulse is switched off, if it length does not change for
                            // OFF_COUNT cycles ( = OFF_COUNT * 20ms )
#define FIRST_PULSE     100 // first pulse starts 200 tics after timer overflow, so we do not compete
                            // with overflow IRQ
#define SPEED_RES       (8/TICS_PER_MICROSECOND)   // All position values in tics are multiplied by this factor. This means, that one 
                            // 'Speed-tic' is 0,125 µs per 20ms cycle. This gives better resolution in defining the speed.
                            // Only when computing the next interrupt time the values are divided by this value again to get
                            // the real 'timer tics'

////////////////////////////////////////////////////////////////////////////////////
// global servo data ( used in ISR )
typedef struct servoData_t {
  struct servoData_t* prevServoDataP;
  uint8_t servoIx :6 ;  // Servo number. On ESP32 this is also the nuber of  the PWM timer
  uint8_t on   :1 ;     // True: create pulse
  uint8_t noAutoff :1;  // don't switch pulses off automatically
                        // on ESP32 'soll' 'ist' and 'inc' are in duty values (  0... DUTY100 )
  int soll;             // Position, die der Servo anfahren soll ( in Tics ). -1: not initialized
  volatile int ist;     // Position, die der Servo derzeit einnimt ( in Tics )
  int inc;              // Schrittweite je Zyklus um Ist an Soll anzugleichen
  uint8_t offcnt;       // counter to switch off pulses if length doesn't change
  #ifdef FAST_PORTWRT
  uint8_t* portAdr;     // port adress related to pin number
  uint8_t  bitMask;     // bitmask related to pin number
  #endif
  uint8_t pin     ;     // pin
} servoData_t ;

////////////////////////////////////////////////////////////////////////////////////////
class MoToServo
{
  private:
    int16_t _lastPos;     // startingpoint of movement
    //uint8_t pin;
    //uint8_t _angle;       // in degrees
    uint16_t _minPw;       // minimum pulse, uS units  
    uint16_t _maxPw;       // maximum pulse, uS units
    servoData_t _servoData;  // Servo data to be used in ISR

	public:
    // don't allow copying and moving of Servo objects
    MoToServo &operator= (const MoToServo & )   =delete;
    MoToServo &operator= (MoToServo && )        =delete;
    MoToServo (const MoToServo & )              =delete;
    MoToServo (MoToServo && )                   =delete;
    
    MoToServo();
    uint8_t attach(int pin); // attach to a pin, sets pinMode, returns 0 on failure, won't
                             // position the servo until a subsequent write() happens
    uint8_t attach( int pin, bool autoOff );        // automatic switch off pulses with constant length
    uint8_t attach(int pin, uint16_t pos0, uint16_t pos180 ); // also sets position values (in us) for angele 0 and 180
    uint8_t attach(int pin, uint16_t pos0, uint16_t pos180, bool autoOff );
    void detach();
    void write(uint16_t);         // specify the angle in degrees, 0 to 180. Values obove 180 are interpreted
                             // as microseconds, limited to MaximumPulse and MinimumPulse
    void setSpeed(int);      // Set movement speed, the higher the faster
                             // Zero means no speed control (default)
    void setSpeed(int,bool); // Set compatibility-Flag (true= compatibility with version V08 and earlier)
    #define HIGHRES 0
    #define SPEEDV08 1
    
    uint8_t moving();        // returns the remaining Way to the angle last set with write() in
                             // in percentage. '0' means, that the angle is reached
    uint8_t read();          // current position in degrees (0...180)
    uint16_t  readMicroseconds();// current pulsewidth in microseconds
    uint8_t attached();
    void setMinimumPulse(uint16_t);  // pulse length for 0 degrees in microseconds, 700uS default
    void setMaximumPulse(uint16_t);  // pulse length for 180 degrees in microseconds, 2300uS default
};

#endif