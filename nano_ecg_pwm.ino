//#define ENABLE_MODE_SELECTOR
#define PINOUT 9

uint8_t pwm_norm_sr[0x20] {
    10, 13, 15, 20, 30, 40, 30, 20, 0, 150, 255, 0, 0, 15, 17, 19, 21,
    24, 30, 40, 50, 60, 55, 50, 30, 20, 10, 10, 10, 10, 10, 10,
};

uint8_t pwm_vtach[0x20] {
    0, 100, 150, 200, 220, 240, 250, 240, 255, 210, 180, 140, 100, 80, 40, 10,
    0, 100, 150, 200, 220, 240, 250, 240, 255, 210, 180, 140, 100, 80, 40, 10,
};

uint8_t pwm_vfib[0x20];     // these values are dynamically computed in setup()

#ifdef ENABLE_MODE_SELECTOR
uint8_t get_mode(void) {
    
}
#endif

inline void disable_resp(void) {
    TCCR2B = 0;
}

inline void enable_resp(void) {
    TCCR2B = 0x3;       // (clk/1024) prescaler
}

// Since there is only one PWM pin used that has been pre-set in setup(), 
// this routine is faster than calling analogWrite()
inline void pwm_dc(const uint8_t duty_cycle) {
    OCR1A = duty_cycle;
}

void pwm_array_sequence(const uint8_t *sequence_array) {
    static uint8_t counter;
    uint8_t value = sequence_array[counter];
    counter = (counter + 1) % 0x1F;
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
ISR(TIMER2_OVF_vect) {
    static unsigned long counter;
    counter = (counter + 1) % 0x1300;
    if(counter == 0)
        PORTB = PORTB ^ 0x4;
}

void setup(void) {
    pinMode(5, OUTPUT);
    pinMode(10, OUTPUT);
    pinMode(PINOUT, OUTPUT);

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
    uint8_t master_delay = 24;
    uint8_t current_mode = 0;
    while(1) {
        pwm_array_sequence(current_sequence);
#ifdef ENABLE_MODE_SELECTOR
        uint8_t mode = get_mode();

#endif
        delay(master_delay);
    }
}
