/*
 *        DIY-ECG-SIM
 *     v1.0.0 JUN/28/2022
 *   Written by Kevin Williams
 *   
 *   WARNING: This program is for educational purposes only.
 *                  NOT FOR MEDICAL USE
 *   
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

//#define ENABLE_CUSTOM_WAVE
#define ENABLE_RESP_SIM         // comment out to disable resp sim
#define ENABLE_MODE_SELECTOR
#define ENABLE_RESP_LED
//#define SPEED_8_MHZ
#define PINOUT 9


#ifndef SPEED_8_MHZ
#define F_CPU 16000000UL
#else
#define F_CPU 8000000UL
#endif
#ifdef ENABLE_CUSTOM_WAVE
#include "wave.h"
#include <string.h>
#endif

enum hr_rate {
    BPM40  = 0x3C,
    BPM80  = 0x1E, 
    BPM120 = 0x14,
    TACH = 0xF,
};

#ifndef SPEED_8_MHZ
enum resp_rate {
    RESP12 = 150,
    RESP38 = 50 
};
#else
enum resp_rate {
    RESP12 = 75,
    RESP38 = 25
};
#endif
uint8_t nsr_fragment[] = {
    35, 38, 35, 20, 20, 20, 25, 5, 
    140, 255, 0, 20, 20, 20, 25, 
    35, 45, 55, 58, 56, 25
};
uint8_t pwm_norm_sr[0x40];

uint8_t pwm_vtach[0x10] {
    0, 100, 150, 200, 220, 240, 250, 240, 
    255, 210, 180, 140, 100, 80, 40, 10,
};

uint8_t pwm_vfib[0x40];     // these values are dynamically computed in setup()

#ifdef ENABLE_CUSTOM_WAVE
uint8_t custom_wave[0x40];
#endif

#ifdef ENABLE_MODE_SELECTOR
// For whatever reason, the optimizer doesn't always inline these functions
// Force inlining with the __attribute__ modifier.  
// PORTD 2,3,4
uint8_t __attribute__((always_inline)) get_mode(void) {
    return((PIND >> 2) & 0x7);
}
#endif

void disable_resp(void) {
    DDRB = DDRB | 0x4;
    TCCR2B = 0;
}

void enable_resp(void) {
#ifdef ENABLE_RESP_SIM
    TCCR2B = 0x7;       // (clk/1024) prescaler
#endif
}
void (*resp_enable_fp)(void) = enable_resp;

// Since there is only one PWM pin used that has been pre-set in setup(), 
// this routine is faster than calling analogWrite()
void __attribute__((always_inline)) pwm_dc(const uint8_t duty_cycle) {
    OCR1A = duty_cycle;
}

void __attribute__((hot)) pwm_array_sequence(const uint8_t *const sequence_array, const uint8_t rate) {
    static uint8_t counter;
    uint8_t value = sequence_array[counter];
    counter = (counter + 1) % rate;
    pwm_dc(value);
    PORTD = PORTD & ~0x20;
    if(value == 255)
        PORTD = PORTD | (1 << PD5);
}


// This routine controls the respiratory simulation. Unfortunately, TIMER2 must be 
// used as TIMER1 is governing the PWM signal. Even with the highest prescaler, the
// timer overflows too quickly. So, we have to use a second counter to further
// divide the signal down to a reasonable rate. The interrupt flag is cleared in
// hardware when this routine is called. 
static long resp_rate = RESP12;
ISR(TIMER2_OVF_vect) {
    static uint8_t counter = 0;
    counter = (counter + 1) % resp_rate;
    if(counter == 0) {
        DDRB = DDRB ^ 0x4;
        PORTB = PORTB & ~0x4;
#ifdef ENABLE_RESP_LED
        PORTB = (PORTB ^ 0x20);
#endif
    }
}

// Since this function is only called once, we can use the Arduino-core functions
// Arduino-core functions should be avoided when optimizing for speed. 
// NOTE: this microcontroller must have at least three, independent interrupt timers
// TIMER0 is reserved for the delay function and can't be used. 
void setup(void) {
    pinMode(5, OUTPUT);
#ifdef ENABLE_RESP_SIM
    pinMode(10, OUTPUT);
#endif
    pinMode(PINOUT, OUTPUT);
#ifdef ENABLE_RESP_LED
    pinMode(13, OUTPUT);
#endif

    // If the arduino is reset with all mode switches high,
    // disable the resp routine by changing the resp_enable_fp 
    // function pointer.
    if(get_mode() == 0x7) 
        resp_enable_fp = disable_resp;

    // Enable non-inverted, high-speed PWM for pin 9 (PB1) on TIMER1
    // OCR1A register determines duty cycle in this mode (range 0 - 255).
    // Registers and constant values taken from ATMega328P datasheet
    cli();
    TCCR1A |= _BV(COM1A1) | _BV(WGM10);
    TCCR1B |= _BV(CS10) | _BV(WGM12);

    // Enable TIMER2 overflow interrupt for respiration routine
    // Allows the respiratory simulation to run asynchronously to the cardiac 
    // waveforms
#ifdef ENABLE_RESP_SIM
    TCCR2A = 0;
    enable_resp(); 
    TCNT2 = 0;
    TIMSK2 = _BV(TOIE2);    // enable TIMER2 overflow interrupt
#endif
    sei();
    
    // precompute vfib values
    // This trig algorithm roughly simulates the random-yet-cyclical nature
    // of V-FIB. Enough to trigger the V-FIB alarms, usually. It will still
    // occasionally be seen as a V-RUN.
    for(uint8_t i = 0; i < sizeof(pwm_vfib); i++)
        pwm_vfib[i] = (50 * (sin(i / 3) * sin((i / 3) * 2))) + 50;

    // create normal sinus rhythm sequence from nsr_fragment and baseline offset
    for(uint8_t i = 0; i < sizeof(pwm_norm_sr); i++)
        pwm_norm_sr[i] = 20;
    for(uint8_t i = 0; i < sizeof(nsr_fragment); i++)
        pwm_norm_sr[i] = nsr_fragment[i];

#ifdef ENABLE_CUSTOM_WAVE
    for(uint8_t i = 0; i < sizeof(custom_wave); i++)
        custom_wave[i] = extern_baseline;
    uint8_t bcount = sizeof(extern_wave_fragment);
    if(bcount > sizeof(custom_wave))
        bcount = sizeof(custom_wave);
    memcpy(custom_wave, extern_wave_fragment, bcount);
#endif
}

void __attribute__((hot)) loop(void) {
    uint8_t *current_sequence = pwm_norm_sr;
    uint8_t heart_rate = BPM80;
    uint8_t master_delay = 25;
    uint8_t current_mode = 0;
    while(1) {
        pwm_array_sequence(current_sequence, heart_rate);
#ifdef ENABLE_MODE_SELECTOR
        uint8_t mode = get_mode();
        if(mode != current_mode) {
            current_mode = mode;
            current_sequence = pwm_norm_sr;
            heart_rate = BPM80;
            resp_rate = RESP12;
            resp_enable_fp();
            //enable_resp();
            switch(current_mode) {
                case 0:             // normal sinus rhythm, 80BPM
                    break;
                case 1:             // normal sinus rhythm, 40BPM
                    heart_rate = BPM40;
                    break;
                case 2:             // normal sinus rhythm, 120BPM
                    heart_rate = BPM120;
                    break;  
                case 3:             // normal sinus rhythm, tach
                    heart_rate = TACH;
                    break; 
                case 4:             // normal sinus rhythm, bpm80, apnea
                    heart_rate = BPM80;
                    disable_resp();
                    break;   
                case 5:             // normal sinus rhythm, bpm80, hyperventilation
                    resp_rate = RESP38;
                    break;
                case 6:             // V-tach
                    current_sequence = pwm_vtach;
                    heart_rate = TACH;
                    resp_rate = RESP38;
                    break;
                case 7:             // V-fib
                    disable_resp();
#ifndef ENABLE_CUSTOM_WAVE
                    current_sequence = pwm_vfib;
#else
                    current_sequence = custom_wave;
#endif
                    break;                                                           
            }
        }

#endif      // ENABLE_MODE_SELECTOR
        delay(master_delay);
    }
}
