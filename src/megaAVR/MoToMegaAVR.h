#ifndef MOTOMEGAAVR_H
#define MOTOMEGAAVR_H
// AVR specific defines for Cpp files

//#warning megaAVR specific cpp includes
void seizeTimerAS();
// reenabling interrupts within an ISR
__attribute(( naked, noinline )) void isrIrqOn ();

extern uint8_t noStepISR_Cnt;   // Counter for nested StepISr-disable

// _noStepIRQ und _stepIRQ werden in servo.cpp und stepper.cpp genutzt
static inline __attribute__((__always_inline__)) void _noStepIRQ() {
        TCA0_SINGLE_INTCTRL &= ~TCA_SINGLE_CMP1_bm ; 
    noStepISR_Cnt++;
    #if defined COMPILING_MOTOSTEPPER_CPP
    SET_TP3;
    #endif
    interrupts(); // allow other interrupts
}

static inline __attribute__((__always_inline__)) void  _stepIRQ(bool force = false) {
    if ( force ) noStepISR_Cnt = 1; //enable IRQ immediately
    if ( noStepISR_Cnt > 0 ) noStepISR_Cnt -= 1; // don't decrease if already 0 ( if enabling IRQ is called too often )
    if ( noStepISR_Cnt == 0 ) {
        #if defined COMPILING_MOTOSTEPPER_CPP
            CLR_TP3;
        #endif
        TCA0_SINGLE_INTCTRL |= TCA_SINGLE_CMP1_bm ; 
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////
#if defined COMPILING_MOTOSERVO_CPP
static inline __attribute__((__always_inline__)) void enableServoIsrAS() {
    // enable compare-A interrupt
    TCA0_SINGLE_INTCTRL |= TCA_SINGLE_CMP0_bm ; 
}


#endif // COMPILING_MOTOSERVO_CPP

/////////////////////////////////////////////////////////////////////////////////////////////////
#if defined COMPILING_MOTOSOFTLED_CPP

#endif // COMPILING_MOTOSOFTLED_CPP

//////////////////////////////////////////////////////////////////////////////////////////////////
// Wird auch in MoToMegaAVR.cpp gebraucht ( SPI-Interrupt )
extern volatile PORT_t  *portSS;
extern uint8_t bitSS;;
#define SET_SS portSS->OUTSET = bitSS 
#define CLR_SS portSS->OUTCLR = bitSS 
#if defined COMPILING_MOTOSTEPPER_CPP
    static uint8_t spiInitialized = false;
    // Macros für fast setting of SS Port	
    volatile PORT_t  *portSS;
    uint8_t bitSS;;

    static inline __attribute__((__always_inline__)) void initSpiAS() {
        if ( spiInitialized ) return;
        // initialize SPI hardware.
        // MSB first, default Clk Level is 0, shift on leading edge
        uint8_t oldSREG = SREG;
        cli();
        pinMode( MOSI, OUTPUT );
        pinMode( SCK, OUTPUT );
        portSS = digitalPinToPortStruct(SS);
        bitSS = digitalPinToBitMask(SS);
        pinMode( SS, OUTPUT );
		// SPI-Mode 0 mit Sendebuffer ( 2.byte kann sofort geschrieben werden )
		// SPI0_CTRLB = SPI_MODE_0_gc | SPI_BUFEN_bm | SPI_BUFWR_bm | SPI_SSD_bm;
		SPI0_CTRLB = SPI_MODE_0_gc | SPI_BUFEN_bm | SPI_BUFWR_bm | SPI_SSD_bm;
		SPI0_INTCTRL = SPI_TXCIE_bm;     // Using Transfer complete ISR
		SPI0_CTRLA =  SPI_PRESC_DIV4_gc |      // prescaler 4 / 4Mhz SPI clk
					( 1 << SPI_MASTER_bp ) | // Master Mode
					( 0 << SPI_DORD_bp ) |   // MSB first
					( 1 << SPI_ENABLE_bp );     // SPI enable
        //digitalWrite( SS, HIGH );
        SET_SS;
        SREG = oldSREG;  // undo cli() 
        spiInitialized = true;  
    }

    static inline __attribute__((__always_inline__)) void startSpiWriteAS( uint8_t spiData[] ) {
        //digitalWrite( SS, LOW );
        CLR_SS;
        SPI0_DATA = spiData[1];
        SPI0_DATA = spiData[0];
    }    
    
    
 
static inline __attribute__((__always_inline__)) void enableStepperIsrAS() {
        TCA0_SINGLE_INTCTRL |= TCA_SINGLE_CMP1_bm ;  
}

#endif // COMPILING_MOTOSTEPPER_CPP


#endif