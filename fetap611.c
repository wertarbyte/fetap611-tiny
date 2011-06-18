#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define LED_BIT 7
#define LED_DDR DDRB
#define LED_PORT PORTB

#define ELEMS(x) ( sizeof(x)/sizeof(*x) )

struct km_con {
	volatile uint8_t *ddr;
	volatile uint8_t *port;
	const uint8_t bit;
};

enum key {
	KEY_0 = 0,
	KEY_1,
	KEY_2,
	KEY_3,
	KEY_4,
	KEY_5,
	KEY_6,
	KEY_7,
	KEY_8,
	KEY_9,
	KEY_HUP,
	KEY_CALL,
	KEY_C,
	KEY_ASTERISK,
	KEY_RIGHT,
	KEY_LEFT,
	KEY_MENU,
	KEY_CNT
};

static struct km_con wires[] = {
	[0] = { &DDRD, &PORTD, PD2 },
	[1] = { &DDRD, &PORTD, PD3 },
	[2] = { &DDRA, &PORTA, PA0 },
	[3] = { &DDRD, &PORTD, PD4 },
	[4] = { &DDRD, &PORTD, PD0 },
	[5] = { &DDRD, &PORTD, PD1 },
	[6] = { &DDRD, &PORTD, PD6 },
	[7] = { &DDRA, &PORTA, PA1 },
	[8] = { &DDRD, &PORTD, PD5 },
};

static const uint8_t keyboard[KEY_CNT][2] = {
	[ KEY_0 ] = { 6, 7 },
	[ KEY_1 ] = { 5, 7 },
	[ KEY_2 ] = { 4, 5 },
	[ KEY_3 ] = { 5, 8 },
	[ KEY_4 ] = { 1, 7 },
	[ KEY_5 ] = { 4, 7 },
	[ KEY_6 ] = { 7, 8 },
	[ KEY_7 ] = { 1, 6 },
	[ KEY_8 ] = { 4, 6 },
	[ KEY_9 ] = { 6, 8 },
	[ KEY_C ] = { 1, 8 },
	[ KEY_CALL ] = { 0, 1 },
	[ KEY_HUP ] = { 2, 3 },
	[ KEY_ASTERISK ] = { 5, 6 },
	[ KEY_RIGHT ] = { 1, 4},
	[ KEY_LEFT ] = { 4, 8},
};

static void wait(uint8_t cs) {
	while (cs--) {
		_delay_ms(10);
	}
}

#define SHORT 30
#define LONG 255

static void press_key(enum key k, uint8_t duration) {
	for (uint8_t i=0; i<2; i++) {
		uint8_t n = keyboard[k][i];
		*wires[n].port |= 1<<wires[n].bit;
	}
	wait(duration);
	for (uint8_t i=0; i<2; i++) {
		uint8_t n = keyboard[k][i];
		*wires[n].port &= ~(1<<wires[n].bit);
	}
	wait(1);
}

#define BELL_DDR DDRB
#define BELL_PORT PORTB
#define BELL_BIT PB0

#define RING_DDR DDRB
#define RING_PORT PORTB
#define RING_PIN PINB
#define RING_BIT PB1

static uint8_t st_ringing = 0;
static uint8_t n_rings = 0;

#define HUP_DDR DDRB
#define HUP_PORT PORTB
#define HUP_PIN PINB
#define HUP_BIT PB6

static uint8_t st_hup = 1;

#define DIAL_DDR DDRB
#define DIAL_PORT PORTB
#define DIAL_PIN PINB
#define DIAL_BIT PB5

static uint8_t st_dial = 1;

// add up the number of dial signals
static uint8_t dial_num = 0;
// count the number of loops since the last dial signal
static uint8_t loopcount_dial = 0;

// count the number of loops since the last ring
static uint8_t loopcount_ring = 0;

static enum phone_state {
	/*
	 * Hung up
	 *
	 * Successors:
	 * -> PICKEDUP (picking up the earphone)
	 * -> RINGING (incoming call)
	 */
	IDLE = 0,
	/*
	 * earphone has been picked up
	 *
	 * Successors:
	 * -> DIALING (once a number is dialed)
	 * -> IDLE (earphone put back)
	 * -> RINGING (incoming call)
	 */
	PICKEDUP,
	/*
	 * a number as been dialed
	 *
	 * Successors:
	 * -> DIALING (another number is dialed)
	 * -> IDLE (hangup)
	 * -> ESTABLISHED (timeout after dialing the last number?)
	 * -> RINGING (incoming call)
	 */
	DIALING,
	/*
	 * the phone number has been dialed by the cellphone
	 *
	 * Successors:
	 * -> IDLE (hangup)
	 */
	ESTABLISHED,
	/*
	 * an incoming call is being detected
	 *
	 * Successors:
	 * -> IDLE (the call is not taken)
	 * -> ESTABLISHED (picked up the phone)
	 */
	RINGING,
} state = IDLE;

static void dialtone(uint8_t enabled) {
	if (enabled) {
		DDRB |= 1<<PB3;
		TCCR1A |= 1<<COM1A0;
		TCCR1B |= 1<<CS10 | 1<<WGM12;
		OCR1A = 0x8E1;
	} else {
		// disable timer
		TCCR1B &= ~(1<<CS10);
		// set output pin to 0
		PORTB &= ~(1<<PB3);
	}
}

static void hangup(void) {
	switch(state) {
		case RINGING:
			/*
			 * A ringing phone without a hung up earphone is unusal;
			 * We handle it by allowing the fork switch to be triggered
			 * and ignoring the hangup signal caused by this
			 */
			break;
		case DIALING:
			// clear dialed numbers
			press_key(KEY_C, LONG);
			dial_num = 0;
			loopcount_dial = 0;
			state = IDLE;
			break;
		case ESTABLISHED:
			// terminate connection
			press_key(KEY_HUP, SHORT);
		default:
			state = IDLE;
	}
	dialtone(0);
}

static void pickup(void) {
	switch(state) {
		case IDLE:
			state = PICKEDUP;
			dialtone(1);
			break;
		case RINGING:
			// accept phone call
			press_key(KEY_CALL, SHORT);
			state = ESTABLISHED;
		default:
			break;
	}
}

static void dial_number(uint8_t n) {
	switch (state) {
		case IDLE:
			if (n == 0) {
				LED_PORT |= 1<<LED_BIT;
				press_key( KEY_HUP, LONG );
				LED_PORT &= ~(1<<LED_BIT);
			}
			if (n == 9) {
				LED_PORT |= 1<<LED_BIT;
				press_key( KEY_HUP, SHORT );
				LED_PORT &= ~(1<<LED_BIT);
			}
			break;
		case PICKEDUP:
			state = DIALING;
			dialtone(0);
		case DIALING:
		case ESTABLISHED:
			press_key(n, SHORT); // for 0-9, the enum is sorted
			while (n--) {
				LED_PORT |= 1<<LED_BIT;
				_delay_ms(40);
				LED_PORT &= ~(1<<LED_BIT);
				_delay_ms(40);
			}
		default:
			break;
	}
}

static void connect(void) {
	press_key( KEY_CALL, SHORT );
	state = ESTABLISHED;
}

void ring_bell(void) {
	BELL_PORT &= ~(1<<BELL_BIT);
	_delay_ms(50);
	BELL_PORT |= 1<<BELL_BIT;
}

static void incoming_call(void) {
	state = RINGING;
	dialtone(0);
	LED_PORT |= 1<<LED_BIT;
	ring_bell();
}

static void incoming_ceased(void) {
	// FIXME we should use two distinguished states here
	if (st_hup) {
		state = IDLE;
	} else {
		state = PICKEDUP;
	}
	LED_PORT &= ~(1<<LED_BIT);
}

int main(void) {
	LED_DDR |= 1<<LED_BIT;
	BELL_DDR |= 1<<BELL_BIT;
	BELL_PORT |= 1<<BELL_BIT; // PNP transistor, HIGH == on, LOW == off

	RING_DDR &= ~(1<<RING_BIT);
	// RING_PORT |= 1<<RING_BIT;

	HUP_DDR &= ~(1<<HUP_BIT); // input
	HUP_PORT |= 1<<HUP_BIT; // enable internal pull-up
	DIAL_DDR &= ~(1<<DIAL_BIT); // input
	DIAL_PORT |= 1<<DIAL_BIT; // enable internal pull-up

	
	for (uint8_t i=0; i<ELEMS(wires); i++) {
		*wires[i].ddr |= 1<<wires[i].bit;
	}

	dialtone(0);
	while(1) {
		uint8_t hup = ( (HUP_PIN & (1<<HUP_BIT)) != 0 );
		if (hup != st_hup) {
			if (hup) {
				hangup();
			} else {
				pickup();
			}
			st_hup = hup;
		}
		uint8_t dial = ( (DIAL_PIN & (1<<DIAL_BIT)) != 0 );
		if (dial != st_dial) {
			if (dial) {
				dial_num++;
				loopcount_dial=0;
			}
			st_dial = dial;
		}
		if (loopcount_dial > 10 && dial_num > 0) {
			dial_number(dial_num%10);
			dial_num = 0;
			loopcount_dial = 0;
		}
		if (state == DIALING && loopcount_dial > 200) {
			connect();
		}
		uint8_t ringing = ( ( RING_PIN & (1<<RING_BIT) ) != 0);
		if (ringing && state != ESTABLISHED) {
			loopcount_ring = 0;
			if (n_rings > 2)
				incoming_call();
			if (!st_ringing)
				n_rings++;
		}
		st_ringing = ringing;
		if (state == RINGING && loopcount_ring > 50) {
			incoming_ceased();
			n_rings = 0;
		}
		loopcount_ring++;
		loopcount_dial++;
		_delay_ms(25);
	}
	return 0;
}
