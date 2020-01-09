/*
  MobaTools.h - a library for model railroaders
  Author: fpm, fpm@mnet-mail.de
  Copyright (c) 2020 All rights reserved.

  Functions for the stepper part of MobaTools
*/
#include <MobaTools.h>
//#define debugTP
//#define debugPrint
#include <utilities/MoToDbg.h>

// Global Data for all instances and classes  --------------------------------
extern uint8_t timerInitialized;

// Variables for servos
static byte servoCount = 0;
#ifndef ESP8266 // following variables used only in 'classic' ISR
    static servoData_t* lastServoDataP = NULL; //start of ServoData-chain
    static servoData_t* pulseP = NULL;         // pulse Ptr in IRQ
    static servoData_t* activePulseP = NULL;   // Ptr to pulse to stop
    static servoData_t* stopPulseP = NULL;     // Ptr to Pulse whose stop time is already in OCR1
    static servoData_t* nextPulseP = NULL;
    static enum { PON, POFF } IrqType = PON; // Cycle starts with 'pulse on'
    static word activePulseOff = 0;     // OCR-value of pulse end 
    static word nextPulseLength = 0;
    static bool speedV08 = true;    // Compatibility-Flag for speed method
/*#else
    static bool speedV08 = false;    // ESP8266 uses alwas high res speed*/
#endif // no ESP8266

inline void _noStepIRQ() {
        #if defined(__AVR_ATmega8__)|| defined(__AVR_ATmega128__)
            TIMSK &= ~( _BV(OCIExB) );    // enable compare interrupts
        #elif defined __AVR_MEGA__
            TIMSKx &= ~_BV(OCIExB) ; 
        #elif defined __STM32F1__
            timer_disable_irq(MT_TIMER, TIMER_STEPCH_IRQ);
            //*bb_perip(&(MT_TIMER->regs).adv->DIER, TIMER_STEPCH_IRQ) = 0;
		#else
			noInterrupts();
        #endif
}
inline void  _stepIRQ() {
        #if defined(__AVR_ATmega8__)|| defined(__AVR_ATmega128__)
            TIMSK |= ( _BV(OCIExB) );    // enable compare interrupts
        #elif defined __AVR_MEGA__
            TIMSKx |= _BV(OCIExB) ; 
        #elif defined __STM32F1__
            //timer_enable_irq(MT_TIMER, TIMER_STEPCH_IRQ) cannot be used, because this also clears pending irq's
            *bb_perip(&(MT_TIMER->regs).adv->DIER, TIMER_STEPCH_IRQ) = 1;
            interrupts();
		#else
			interrupts();
        #endif
}

///////////////////////  Interrupt for Servos ////////////////////////////////////////////////////////////////////
#ifdef ESP8266
#define startServoPulse(pin,width) startWaveformMoTo(pin, width/TICS_PER_MICROSECOND/SPEED_RES, TIMERPERIODE-width/TICS_PER_MICROSECOND/SPEED_RES,0)

// --------------------- Pulse-interrupt for ESP8266 --------------------------
// This ISR is fired at the falling edge of the servo pulse. It is specific to every servo Objekt and
// computes the length of the next pulse. The pulse itself is created by the core_esp8266_waveform routines.
void ICACHE_RAM_ATTR ISR_Servo( servoData_t *_servoData ) {
    if ( _servoData->ist != _servoData->soll ) {
        //SetGPIO(0b100000);
        if ( _servoData->ist > _servoData->soll ) {
            _servoData->ist -= _servoData->inc;
            if ( _servoData->ist < _servoData->soll ) _servoData->ist = _servoData->soll;
        } else {
            _servoData->ist += _servoData->inc;
            if ( _servoData->ist > _servoData->soll ) _servoData->ist = _servoData->soll;
        }
        startServoPulse(_servoData->pin, _servoData->ist);

        //ClearGPIO(0b100000);
    } else if ( !_servoData->noAutoff ) { // no change in pulse length, look for autooff
        if ( --_servoData->offcnt == 0 ) {
            // switch off pulses
            stopWaveformMoTo( _servoData->pin );
        }
    }
    
}
#else //---------------------- Timer-interrupt for non ESP8266 -----------------------------
// create overlapping servo pulses
// Positions of servopulses within 20ms cycle are variable, max 2 pulses at the same time
// 27.9.15 with variable overlap, depending on length of next pulse: 16 Servos
// 2.1.16 Enable interrupts after timecritical path (e.g. starting/stopping servo pulses)
//        so other timecritical tasks can interrupt (nested interrupts)
// 6.6.19 Because stepper IRQ now can last very long, it is disabled during servo IRQ
static bool searchNextPulse() {
    SET_TP4;
   while ( pulseP != NULL && pulseP->soll < 0 ) {
        //SET_TP4;
        pulseP = pulseP->prevServoDataP;
        //CLR_TP4;
    }
    //CLR_TP2;
    if ( pulseP == NULL ) {
        // there is no more pulse to start, we reached the end
        CLR_TP4;
        return false;
    } else { // found pulse to output
        //SET_TP2;
        if ( pulseP->ist == pulseP->soll ) {
            // no change of pulselength
            if ( pulseP->offcnt > 0 ) pulseP->offcnt--;
        } else if ( pulseP->ist < pulseP->soll ) {
            pulseP->offcnt = OFF_COUNT;
            if ( pulseP->ist < 0 ) pulseP->ist = pulseP->soll; // first position after attach
            else pulseP->ist += pulseP->inc;
            if ( pulseP->ist > pulseP->soll ) pulseP->ist = pulseP->soll;
        } else {
            pulseP->offcnt = OFF_COUNT;
            pulseP->ist -= pulseP->inc;
            if ( pulseP->ist < pulseP->soll ) pulseP->ist = pulseP->soll;
        } 
        CLR_TP4;
        return true;
    } 
} //end of 'searchNextPulse'

// ---------- OCRxA Compare Interrupt used for servo motor (overlapping pulses) ----------------
#ifdef __AVR_MEGA__
ISR ( TIMERx_COMPA_vect) {
    uint8_t saveTIMSK;
    saveTIMSK = TIMSKx; // restore IE for stepper later ( maybe it is not enabled)
#elif defined __STM32F1__
void ISR_Servo( void) {
    uint16_t OCRxA;
#endif
    SET_TP3;
    // Timer1 Compare A, used for servo motor
    if ( IrqType == POFF ) { // Pulse OFF time
        SET_TP2; // Oszimessung Dauer der ISR-Routine OFF
        //SET_TP3; // Oszimessung Dauer der ISR-Routine
        IrqType = PON ; // it's (nearly) always alternating
        // switch off previous started pulse
        #ifdef FAST_PORTWRT
        *stopPulseP->portAdr &= ~stopPulseP->bitMask;
        #else
        digitalWrite( stopPulseP->pin, LOW );
        #endif
        if ( nextPulseLength > 0 ) {
            // there is a next pulse to start, compute starttime 
            // set OCR value to next starttime ( = endtime of running pulse -overlap )
            // next starttime must behind actual timervalue and endtime of next pulse must
            // lay after endtime of runningpuls + safetymargin (it may be necessary to start
            // another pulse between these 2 ends)
            word tmpTCNT1 = GET_COUNT + MARGINTICS/2;
            #ifdef __AVR_MEGA__
            _noStepIRQ();   // Stepper IRQ may be too long and must not interrupt the servo IRQ
            interrupts();
            #endif
            //CLR_TP3 ;
            OCRxA = max ( ((long)activePulseOff + (long) MARGINTICS - (long) nextPulseLength), ( tmpTCNT1 ) );
        } else {
            // we are at the end, no need to start another pulse in this cycle
            if ( activePulseOff ) {
                // there is still a running pulse to stop
                //SET_TP1; // Oszimessung Dauer der ISR-Routine
                OCRxA = activePulseOff;
                IrqType = POFF;
                stopPulseP = activePulseP;
                activePulseOff = 0;
                //CLR_TP1; // Oszimessung Dauer der ISR-Routine
            } else { // was last pulse, start over
                pulseP = lastServoDataP;
                nextPulseLength = 0;
                OCRxA = FIRST_PULSE;
            }
        }
        CLR_TP2; // Oszimessung Dauer der ISR-Routine OFF
    } else { // Pulse ON - time
        //SET_TP2; // Oszimessung Dauer der ISR-Routine ON
        //if ( pulseP == lastServoDataP ) SET_TP3;
        // look for next pulse to start
        // do we know the next pulse already?
        if ( nextPulseLength > 0 ) {
            // yes we know, start this pulse and then look for next one
            word tmpTCNT1= GET_COUNT-4; // compensate for computing time
            if ( nextPulseP->on && (nextPulseP->offcnt+nextPulseP->noAutoff) > 0 ) {
                // its a 'real' pulse, set output pin
                //CLR_TP1;
                #ifdef FAST_PORTWRT
                *nextPulseP->portAdr |= nextPulseP->bitMask;
                #else
                digitalWrite( nextPulseP->pin, HIGH );
                #endif
            }
            #ifdef __AVR_MEGA__
            _noStepIRQ(); // Stepper ISR may be too long  and must not interrupt the servo IRQ
            interrupts(); // the following isn't time critical, so allow nested interrupts
            #endif
            //SET_TP3;
            // the 'nextPulse' we have started now, is from now on the 'activePulse', the running activPulse is now the
            // pulse to stop next.
            stopPulseP = activePulseP; // because there was a 'nextPulse' there is also an 'activPulse' which is the next to stop
            OCRxA = activePulseOff;
            activePulseP = nextPulseP;
            activePulseOff = activePulseP->ist/SPEED_RES + tmpTCNT1; // end of actually started pulse
            nextPulseLength = 0;
            //SET_TP1;
        }
        if ( searchNextPulse() ) {
            // found a pulse
            if ( activePulseOff == 0 ) {
                // it is the first pulse in the sequence, start it
                TOG_TP2;
                activePulseP = pulseP; 
                activePulseOff = pulseP->ist/SPEED_RES + GET_COUNT - 4; // compensate for computing time
                if ( pulseP->on && (pulseP->offcnt+pulseP->noAutoff) > 0 ) {
                    // its a 'real' pulse, set output pin
                    #ifdef FAST_PORTWRT
                    *pulseP->portAdr |= pulseP->bitMask;
                    #else
                    digitalWrite( pulseP->pin, HIGH );
                    #endif
                }
                word tmpTCNT1 = GET_COUNT;
                #ifdef __AVR_MEGA__
                _noStepIRQ(); // Stepper ISR may be too long  and must not interrupt the servo IRQ
                interrupts(); // the following isn't time critical, so allow nested interrupts
                #endif
                //SET_TP3;
                // look for second pulse
                //SET_TP4;
                pulseP = pulseP->prevServoDataP;
                //CLR_TP4;
                if ( searchNextPulse() ) {
                    // there is a second pulse - this is the 'nextPulse'
                    nextPulseLength = pulseP->ist/SPEED_RES;
                    nextPulseP = pulseP;
                    //SET_TP4;
                    pulseP = pulseP->prevServoDataP;
                    //CLR_TP4;
                    // set Starttime for 2. pulse in sequence
                    OCRxA = max ( ((long)activePulseOff + (long) MARGINTICS - (long) nextPulseLength), ( (long)tmpTCNT1 + MARGINTICS/2 ) );
                } else {
                    // no next pulse, there is only one pulse
                    OCRxA = activePulseOff;
                    activePulseOff = 0;
                    stopPulseP = activePulseP;
                    IrqType = POFF;
                }
                TOG_TP2;
            } else {
                // its a pulse in sequence, so this is the 'nextPulse'
                nextPulseLength = pulseP->ist/SPEED_RES;
                nextPulseP = pulseP;
                //SET_TP4;
                pulseP = pulseP->prevServoDataP;
                //CLR_TP4;
                IrqType = POFF;
            }
        } else {
            // found no pulse, so the last one is running or no pulse at all
            
            if ( activePulseOff == 0 ) {
                // there wasn't any pulse, restart
                pulseP = lastServoDataP;
                nextPulseLength = 0;
                OCRxA = FIRST_PULSE;
            } else {
                // is last pulse, don't start a new one
                IrqType = POFF;
            }
        }
        //CLR_TP2; CLR_TP3; // Oszimessung Dauer der ISR-Routine ON
    } //end of 'pulse ON'
    #ifdef __STM32F1__
    timer_set_compare(MT_TIMER,  SERVO_CHN, OCRxA);
    #endif 
    //CLR_TP1; CLR_TP3; // Oszimessung Dauer der ISR-Routine
    #ifdef __AVR_MEGA__
    TIMSKx = saveTIMSK;      // retore Interrupt enable reg
    #endif
    CLR_TP3;
}

#endif // not ESP8266
// ------------ end of Interruptroutines ------------------------------
///////////////////////////////////////////////////////////////////////////////////
// --------- Class MoToServo ---------------------------------
// Class-specific Variables

const byte NO_ANGLE = 0xff;
const byte NO_PIN = 0xff;

MoToServo::MoToServo() //: _servoData.pin(NO_PIN),_angle(NO_ANGLE),_min16(1000/16),_max16(2000/16)
{   _servoData.servoIx = servoCount++;
    _servoData.soll = -1;    // = not initialized
    _servoData.pin = NO_PIN;
    _minPw = MINPULSEWIDTH ;
    _maxPw = MAXPULSEWIDTH ;
    #ifndef ESP8266 // there is no servochain on ESP8266
    noInterrupts(); // Add to servo-chain
    _servoData.prevServoDataP = lastServoDataP;
    lastServoDataP = &_servoData;
    interrupts();
    #endif
}

void MoToServo::setMinimumPulse(uint16_t t)
{   _minPw = constrain( t, MINPULSEWIDTH,MAXPULSEWIDTH);
}

void MoToServo::setMaximumPulse(uint16_t t)
{   _maxPw = constrain( t, MINPULSEWIDTH,MAXPULSEWIDTH);
}


uint8_t MoToServo::attach(int pinArg) {
    return attach( pinArg, MINPULSEWIDTH, MAXPULSEWIDTH, false );
}
uint8_t MoToServo::attach(int pinArg, bool autoOff ) {
    return attach( pinArg, MINPULSEWIDTH, MAXPULSEWIDTH, autoOff );
}
uint8_t MoToServo::attach(int pinArg, uint16_t pmin, uint16_t pmax ) {
    return attach( pinArg, pmin, pmax, false );
}

uint8_t MoToServo::attach( int pinArg, uint16_t pmin, uint16_t pmax, bool autoOff ) {
    // return false if already attached or too many servos
    if ( _servoData.pin != NO_PIN ||  _servoData.servoIx >= MAX_SERVOS ) return 0;
    #ifdef ESP8266 // check pinnumber
        if ( pinArg <0 || pinArg >15 || gpioUsed(pinArg ) ) return 0;
        setGpio(pinArg);    // mark pin as used
    #endif   
    // set pulselength for angle 0 and 180
    _minPw = constrain( pmin, MINPULSEWIDTH, MAXPULSEWIDTH );
    _maxPw = constrain( pmax, MINPULSEWIDTH, MAXPULSEWIDTH );
	//DB_PRINT( "pin: %d, pmin:%d pmax%d autoOff=%d, _min16=%d, _max16=%d", pinArg, pmin, pmax, autoOff, _min16, _max16);
    
    // intialize objectspecific data
    _lastPos = 1500*TICS_PER_MICROSECOND*SPEED_RES ;    // initalize to middle position
    _servoData.soll = -1;  // invalid position -> no pulse output
    _servoData.ist = -1;   
    _servoData.inc = 2000*SPEED_RES;  // means immediate movement
    _servoData.pin = pinArg;
    _servoData.on = false;  // create no pulses until next write
    _servoData.noAutoff = autoOff?0:1 ;  
    #ifdef FAST_PORTWRT
    // compute portaddress and bitmask related to pin number
    _servoData.portAdr = (byte *) pgm_read_word_near(&port_to_output_PGM[pgm_read_byte_near(&digital_pin_to_port_PGM[ pinArg])]);
    _servoData.bitMask = pgm_read_byte_near(&digital_pin_to_bit_mask_PGM[pinArg]);
    //DB_PRINT( "Idx: %d Portadr: 0x%x, Bitmsk: 0x%x", _servoData.servoIx, _servoData.portAdr, _servoData.bitMask );
	#endif
    pinMode (_servoData.pin,OUTPUT);
    digitalWrite( _servoData.pin,LOW);
    #ifdef ESP8266
        // assign an ISR to the pin
        gpioTab[gpio2ISRx(_servoData.pin)].MoToISR = (void (*)(void*))ISR_Servo;
        gpioTab[gpio2ISRx(_servoData.pin)].IsrData = &_servoData;
        attachInterrupt( _servoData.pin, gpioTab[gpio2ISRx(_servoData.pin)].gpioISR, FALLING );
    #else
        if ( !timerInitialized) seizeTimer1();
        // initialize servochain pointer and ISR if not done already
        noInterrupts();
        if ( pulseP == NULL ) {
            pulseP = lastServoDataP;
            #ifdef __STM32F1__
            timer_attach_interrupt(MT_TIMER, TIMER_SERVOCH_IRQ, ISR_Servo );
            #endif
        
            // enable compare-A interrupt
            #if defined(__AVR_ATmega8__)|| defined(__AVR_ATmega128__)
            TIMSK |=  _BV(OCIExA);   
            #elif defined __AVR_MEGA__
            //DB_PRINT( "IniOCR: %d", OCRxA );
            TIMSKx |=  _BV(OCIExA) ; 
            //DB_PRINT( "AttOCR: %d", OCRxA );
            #elif defined __STM32F1__
                timer_cc_enable(MT_TIMER, SERVO_CHN);
            #endif
        }
         interrupts();
    #endif // no ESP8266
    return 1;
}

void MoToServo::detach()
{
    if ( _servoData.pin == NO_PIN ) return; // only if servo is attached
    byte tPin = _servoData.pin;
    while( digitalRead( _servoData.pin ) ); // don't detach during an active pulse
    noInterrupts();
    _servoData.on = false;  
    _servoData.soll = -1;  
    _servoData.ist = -1;  
    _servoData.pin = NO_PIN;  
    interrupts();
    #ifdef ESP8266
        stopWaveformMoTo(tPin); //stop creating pulses
        clrGpio(tPin);
        detachInterrupt( tPin );
    #endif
    pinMode( tPin, INPUT );
}

void MoToServo::write(uint16_t angleArg)
{   // set position to move to
    // values between 0 and 180 are interpreted as degrees,
    // values between MINPULSEWIDTH and MAXPULSEWIDTH are interpreted as microseconds
    static int newpos;
    bool startPulse = false;    // only for esp8266
    SET_TP1;
    #ifdef __AVR_MEGA__
	//DB_PRINT( "Write: angleArg=%d, Soll=%d, OCR=%u", angleArg, _servoData.soll, OCRxA );
    #endif
    if ( _servoData.pin != NO_PIN ) { // only if servo is attached
        //Serial.print( "Pin:" );Serial.print (_servoData.pin);Serial.print("Wert:");Serial.println(angleArg);
        #ifdef __AVR_MEGA__
		//DB_PRINT( "Stack=0x%04x, &sIx=0x%04x", ((SPH&0x7)<<8)|SPL, &_servoData.servoIx );
        #endif
        if ( angleArg <= 255) {
            // pulse width as degrees (byte values are always degrees) 09-02-2017
            angleArg = min( 180,(int)angleArg);
            newpos = map( angleArg, 0,180, _minPw, _maxPw ) * TICS_PER_MICROSECOND * SPEED_RES;
        } else {
            // pulsewidth as microseconds
            newpos = constrain( angleArg, _minPw, _maxPw ) * TICS_PER_MICROSECOND * SPEED_RES;
            
        }
        if ( _servoData.soll < 0 ) {
            // Serial.println( "first write");
            // this is the first pulse to be created after attach
            _servoData.on = true;
            startPulse = true;
            _lastPos = newpos;
            noInterrupts();
            _servoData.soll= newpos ; 
            _servoData.ist= newpos ; // .ist =.soll  -> will jump to .soll immediately
            interrupts();
            
        }
        else if ( newpos != _servoData.soll ) {
            // position has changed, store old position, set new position
            _lastPos = _servoData.soll;
            noInterrupts();
            _servoData.soll= newpos ;
            interrupts();
        }
        #ifdef ESP8266 // start creating pulses?
            if ( (startPulse) || (_servoData.offcnt+_servoData.noAutoff) == 0  ) {
                // first pulse after attach, or pulses have been switch off by autoff
                startServoPulse(_servoData.pin, _servoData.ist);
                DB_PRINT( "start pulses at pin %d, ist=%d, soll=%d", _servoData.pin, _servoData.ist, _servoData.soll );
            }
        #endif
        _servoData.offcnt = OFF_COUNT;   // auf jeden Fall wieder Pulse ausgeben
    }
    DB_PRINT( "Soll=%d, Ist=%d, Ix=%d, inc=%d, SR=%d", _servoData.soll,_servoData.ist, _servoData.servoIx, _servoData.inc, SPEED_RES );
    delay(2);
    CLR_TP1;
}

void MoToServo::setSpeed( int speed, bool compatibility ) {
    // set global compatibility-Flag
    #ifndef ESP8266
    speedV08 = compatibility;   // not on ESP8266
    #endif
    setSpeed( speed );
}

void MoToServo::setSpeed( int speed ) {
    // Set increment value for movement to new angle
    if ( _servoData.pin != NO_PIN ) { // only if servo is attached
        #ifndef ESP8266
        if ( speedV08 ) speed *= SPEED_RES;
        #endif
        noInterrupts();
        if ( speed == 0 )
            _servoData.inc = 2000*SPEED_RES;  // means immediate movement
        else
            _servoData.inc = speed;
        interrupts();
    }
}

uint8_t MoToServo::read() {
    // get position in degrees
    int offset;
    if ( _servoData.pin == NO_PIN ) return -1; // Servo not attached
    offset = (_maxPw - _minPw)/180/2;
    return map( readMicroseconds() + offset, _minPw, _maxPw, 0, 180 );
}

int MoToServo::readMicroseconds() {
    // get position in microseconds
    int value;
    if ( _servoData.pin == NO_PIN ) return -1; // Servo not attached
    noInterrupts();
    value = _servoData.ist;
    interrupts();
    if ( value < 0 ) value = _servoData.soll; // there is no valid actual vlaue
    DB_PRINT( "Ist=%d, Soll=%d, TpM=%d, SR=%d", value, _servoData.soll, TICS_PER_MICROSECOND, SPEED_RES );
    return value/TICS_PER_MICROSECOND/SPEED_RES;   
}

uint8_t MoToServo::moving() {
    // return how much still to move (percentage)
    if ( _servoData.pin == NO_PIN ) return 0; // Servo not attached
    long total , remaining;
    total = abs( _lastPos - _servoData.soll );
    noInterrupts(); // disable interrupt, because integer _servoData.ist is changed in interrupt
    remaining = abs( _servoData.soll - _servoData.ist );
    interrupts();  // allow interrupts again
    if ( remaining == 0 ) return 0;
    return ( remaining * 100 ) /  total +1;
}

uint8_t MoToServo::attached()
{
    return ( _servoData.pin != NO_PIN );
}

