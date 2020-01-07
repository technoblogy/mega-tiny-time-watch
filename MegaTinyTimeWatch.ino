/* Mega Tiny Time Watch

   David Johnson-Davies - www.technoblogy.com - 7th January 2020
   ATtiny414 @ 5 MHz (internal oscillator; BOD disabled)
   
   CC BY 4.0
   Licensed under a Creative Commons Attribution 4.0 International license: 
   http://creativecommons.org/licenses/by/4.0/
*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

const int Tickspersec = 250;              // Units of MyDelay()

volatile int Timeout;
volatile int Offset = 0;                  // For setting time
volatile uint8_t Cycle = 0;

volatile unsigned int Secs = 0;
volatile int Hours = 0;                   // From 0 to 11, or 12 = off.
volatile int Fivemins = 0;                // From 0 to 11, or 12 = off.

volatile int ShowTime = false;

// Pin assignments

int Pins[6][6] = {{ -1, -1, -1,  8,  6,  4 },
                  { -1, -1, -1, -1, -1, -1 },
                  { -1, -1, -1, -1, -1, -1 },
                  {  7, -1, -1, -1, 11,  9 },
                  {  0, -1, -1, 10, -1,  2 },
                  {  5, -1, -1,  3,  1, -1 }};

// Display multiplexer **********************************************

void DisplaySetup () {
  // Set up Timer/Counter TCB to multiplex the display
  TCB0.CCMP = 19999;                                  // Divide 5MHz by 20000 = 250Hz
  TCB0.CTRLA = TCB_CLKSEL_CLKDIV1_gc | TCB_ENABLE_bm; // Enable timer, divide by 1
  TCB0.CTRLB = 0;                                     // Periodic Interrupt Mode
  TCB0.INTCTRL = TCB_CAPT_bm;                         // Enable interrupt
}

// Timer/Counter TCB interrupt - multiplexes the display and counts ticks
ISR(TCB0_INT_vect) {
  TCB0.INTFLAGS = TCB_CAPT_bm;                        // Clear the interrupt flag
  DisplayNextRow();
  Timeout--;
  Offset++;
}

void DisplayOn () {
  // Turn off all the pullups
  PORTA.PIN0CTRL = 0;
  PORTA.PIN1CTRL = 0;
  PORTA.PIN4CTRL = 0;
  PORTA.PIN5CTRL = 0;
  PORTA.PIN6CTRL = 0;
  TCB0.INTCTRL = TCB_CAPT_bm;                         // Enable interrupt
}

void DisplayOff () {
  TCB0.INTCTRL = 0;                                   // Disable interrupt
  PORTA.DIR = 0;                                      // All PORTA pins inputs
  // Turn on all the pullups for minimal power in sleep
  PORTA.PIN0CTRL = PORT_PULLUPEN_bm;                  // UPDI
  PORTA.PIN1CTRL = PORT_PULLUPEN_bm;
  PORTA.PIN4CTRL = PORT_PULLUPEN_bm;
  PORTA.PIN5CTRL = PORT_PULLUPEN_bm;
  PORTA.PIN6CTRL = PORT_PULLUPEN_bm;
}
  
void DisplayNextRow() {
  Cycle++;
  uint8_t row = Cycle & 0x03;
  if (row > 0) row = row + 2;                         // Skip PA2 and PA3
  uint8_t bits = 0;
  for (int i=0; i<6; i++) {
    if (Hours == Pins[row][i]) bits = bits | 1<<(i+1);
    if (Fivemins == Pins[row][i]) bits = bits | 1<<(i+1);
  }
  PORTA.DIR = 1<<(row+1) | bits;                      // Make outputs for lit LED
  PORTA.OUT = bits;                                   // Set outputs high
}

// Delay in 1/250 of a second
void MyDelay (int count) {
  Timeout = count;
  while (Timeout);
}

// Real-Time Clock **********************************************

void RTCSetup () {
  uint8_t temp;
  // Initialize 32.768kHz Oscillator:

  // Disable oscillator:
  temp = CLKCTRL.XOSC32KCTRLA & ~CLKCTRL_ENABLE_bm;

  // Enable writing to protected register
  CPU_CCP = CCP_IOREG_gc;
  CLKCTRL.XOSC32KCTRLA = temp;

  while (CLKCTRL.MCLKSTATUS & CLKCTRL_XOSC32KS_bm);   // Wait until XOSC32KS is 0
  
  temp = CLKCTRL.XOSC32KCTRLA & ~CLKCTRL_SEL_bm;      // Use External Crystal
  
  // Enable writing to protected register
  CPU_CCP = CCP_IOREG_gc;
  CLKCTRL.XOSC32KCTRLA = temp;
  
  temp = CLKCTRL.XOSC32KCTRLA | CLKCTRL_ENABLE_bm;    // Enable oscillator
  
  // Enable writing to protected register
  CPU_CCP = CCP_IOREG_gc;
  CLKCTRL.XOSC32KCTRLA = temp;
  
  // Initialize RTC
  while (RTC.STATUS > 0);                             // Wait until registers synchronized

  // 32.768kHz External Crystal Oscillator (XOSC32K)
  RTC.CLKSEL = RTC_CLKSEL_TOSC32K_gc;
  
  RTC.DBGCTRL = RTC_DBGRUN_bm; // Run in debug: enabled

  RTC.PITINTCTRL = RTC_PI_bm;                         // Periodic Interrupt: enabled
  
  // RTC Clock Cycles 32768, enabled ie 1Hz interrupt
  RTC.PITCTRLA = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm;
}

// Interrupt Service Routine called every second
ISR(RTC_PIT_vect) {
  RTC.PITINTFLAGS = RTC_PI_bm;                        // Clear interrupt flag
  Secs = (Secs + 1) % 43200;                          // Wrap around after 12 hours
}

// Show time button **********************************************

void ButtonEnable () {
  PORTA.PIN2CTRL = PORT_PULLUPEN_bm | PORT_ISC_LEVEL_gc; // Pullup, Trigger low level
}

ISR(PORTA_PORT_vect) {
  PORTA.PIN2CTRL = PORT_PULLUPEN_bm;                  // Disable button
  PORTA.INTFLAGS = PORT_INT2_bm;                      // Clear PA2 interrupt flag
  ShowTime = true;
}

// Setup **********************************************

void SleepSetup () {
  SLPCTRL.CTRLA |= SLPCTRL_SMODE_PDOWN_gc;
  SLPCTRL.CTRLA |= SLPCTRL_SEN_bm;
}

void SetTime () {
  unsigned int secs = 0;
  ButtonEnable();
  while (!ShowTime) {
    Fivemins = (secs/300)%12;
    Hours = ((secs+1799)/3600)%12;
    // Write time to global Secs
    Secs = secs + (Offset/Tickspersec);
    DisplayOn();
    MyDelay(Tickspersec);
    DisplayOff();
    secs = secs + 300;
  }
}

void setup () {
  // Turn on pullups on unused pins for minimal power in sleep
  PORTA.PIN3CTRL = PORT_PULLUPEN_bm;
  PORTA.PIN7CTRL = PORT_PULLUPEN_bm;
  PORTB.PIN0CTRL = PORT_PULLUPEN_bm;
  PORTB.PIN1CTRL = PORT_PULLUPEN_bm;
  //
  DisplaySetup();
  SleepSetup();
  // Set time on power-on
  SetTime();
  RTCSetup();
}

void loop () {
  unsigned int secs;
  if (ShowTime) {
    cli(); secs = Secs; sei();
    Hours = ((secs+1800)/3600)%12;
    Fivemins = 12;
    int Mins = (secs/60)%60;
    int From = Mins/5;
    int Count = Mins%5;
    DisplayOn();
    for (int i=0; i<5-Count; i++) {
      Fivemins = From; MyDelay(Tickspersec/5);
      Fivemins = 12; MyDelay(Tickspersec/5);
    }
    for (int i=0; i<Count; i++) {
      Fivemins = (1+From)%12; MyDelay(Tickspersec/5);
      Fivemins = 12; MyDelay(Tickspersec/5);
    }
    DisplayOff();
    ShowTime = false;
    ButtonEnable();
  }
  sleep_cpu();
}
