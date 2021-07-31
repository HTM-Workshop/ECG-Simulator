#define ENABLE_MODE_SELECTOR
//#define ENABLE_RESP_LED
//#define SPEED_8_MHZ
#define PINOUT 9
enum hr_rate {
    BPM40  = 0x3C,
    BPM80  = 0x1E, 
    BPM120 = 0x14,
    TACH = 0xF
};

#ifndef SPEED_8_MHZ
enum resp_rate {
    RESP12 = 0x1300,
    RESP38 = 0x600
};
#else
enum resp_rate {
    RESP12 = 0x980,
    RESP38 = 0x300
};
#endif
uint8_t pwm_norm_sr[0x40] {
    10, 13, 15, 20, 30, 40, 30, 20, 0, 150, 255, 0, 0, 15, 17, 19, 21,
    24, 30, 40, 50, 60, 55, 50, 30, 20, 10, 10, 10, 10, 10, 10,
};

uint8_t pwm_vtach[0x20] {
    0, 100, 150, 200, 220, 240, 250, 240, 255, 210, 180, 140, 100, 80, 40, 10,
    0, 100, 150, 200, 220, 240, 250, 240, 255, 210, 180, 140, 100, 80, 40, 10,
};

uint8_t pwm_vfib[0x20];     // these values are dynamically computed in setup()

#ifdef ENABLE_MODE_SELECTOR
// PORTD 2,3,4
static inline uint8_t get_mode(void) {
    return((PIND >> 2) & 0x7);
}
#endif

static inline void disable_resp(void) {
    TCCR2B = 0;
}

static inline void enable_resp(void) {
    TCCR2B = 0x3;       // (clk/1024) prescaler
}

// Since there is only one PWM pin used that has been pre-set in setup(), 
// this routine is faster than calling analogWrite()
static inline void pwm_dc(const uint8_t duty_cycle) {
    OCR1A = duty_cycle;
}

void pwm_array_sequence(const uint8_t *sequence_array, const uint8_t rate) {
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
    static unsigned long counter;
    counter = (counter + 1) % resp_rate;
    if(counter == 0) {
        DDRB = DDRB ^ 0x4;
        PORTB = PORTB & ~0x4;
#ifdef ENABLE_RESP_LED
        PORTB = (PORTB ^ 0x1);
#endif
    }
}

void setup(void) {
    pinMode(5, OUTPUT);
    pinMode(10, OUTPUT);
    pinMode(PINOUT, OUTPUT);
#ifdef ENABLE_RESP_LED
    pinMode(8, OUTPUT);
#endif

    // Enable non-inverted, high-speed PWM for pin 9 (PB1) on TIMER1
    // OCR1A register determines duty cycle in this mode (range 0 - 255).
    // Registers and constant values taken from ATMega328P datasheet
    TCCR1A |= _BV(COM1A1) | _BV(WGM10);
    TCCR1B |= _BV(CS10) | _BV(WGM12);

    // Enable TIMER2 overflow interrupt for respiration routine
    // Allows the respiratory simulation to run asynchronously to the cardiac 
    // waveforms
    cli();
    TCCR2A = 0;
    enable_resp(); 
    TCNT2 = 0;
    TIMSK2 = _BV(TOIE2);    // enable TIMER2 overflow interrupt
    sei();
    
    // precompute vfib values 
    for(uint8_t i = 0; i < 0x20; i++)
        pwm_vfib[i] = (50 * (sin(i / 3) * sin((i / 3) * 2))) + 50;
}

void loop(void) {
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
            enable_resp();
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
                    resp_rate = RESP38;
                    break;
                case 7:             // V-fib
                    current_sequence = pwm_vfib;
                    disable_resp();
                    break;                                                           
            }
        }

#endif
        delay(master_delay);
    }
}
