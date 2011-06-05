#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define LED_BIT 7
#define LED_DDR DDRB
#define LED_PORT PORTB

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
	KEY_CNT
};

#define A { &DDRD, &PORTD, PD2 }
#define B { &DDRD, &PORTD, PD3 }
#define C { &DDRA, &PORTA, PA0 }
#define D { &DDRD, &PORTD, PD4 }
#define E { &DDRD, &PORTD, PD0 }
#define F { &DDRD, &PORTD, PD1 }
#define G { &DDRD, &PORTD, PD6 }
#define H { &DDRA, &PORTA, PA1 }
#define I { &DDRD, &PORTD, PD5 }

static const struct km_con keyboard[][2] = {
	[ KEY_0 ] = { G, H }, 
	[ KEY_1 ] = { F, H }, 
	[ KEY_2 ] = { E, F }, 
	[ KEY_3 ] = { F, I }, 
	[ KEY_4 ] = { B, H }, 
	[ KEY_5 ] = { E, H }, 
	[ KEY_6 ] = { H, I }, 
	[ KEY_7 ] = { G, B }, 
	[ KEY_8 ] = { E, G }, 
	[ KEY_9 ] = { G, I }, 
	[ KEY_C ] = { B, I },
	[ KEY_CALL ] = { A, B },
	[ KEY_HUP ] = { C, D },
};

static void wait(uint8_t ms) {
	while (ms--) {
		_delay_ms(1);
	}
}

#define SHORT 40
#define LONG 200

static void press_key(enum key k, uint8_t duration) {
	for (uint8_t i=0; i<2; i++) {
		*(keyboard[k][i].port) |= 1<<keyboard[k][i].bit;
	}
	wait(duration);
	for (uint8_t i=0; i<2; i++) {
		*(keyboard[k][i].port) &= ~(1<<keyboard[k][i].bit);
	}
}

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

static enum phone_state {
	/*
	 * Hung up
	 *
	 * Successors:
	 * -> PICKEDUP (picking up the earphone)
	 */
	IDLE = 0,
	/*
	 * earphone has been picked up
	 *
	 * Successors:
	 * -> DIALING (once a number is dialed)
	 * -> IDLE (earphone put back)
	 */
	PICKEDUP,
	/*
	 * a number as been dialed
	 *
	 * Successors:
	 * -> DIALING (another number is dialed)
	 * -> IDLE (hangup)
	 * -> ESTABLISHED (timeout after dialing the last number?)
	 */
	DIALING,
	/*
	 * the phone number has been dialed by the cellphone
	 *
	 * Successors:
	 * -> IDLE (hangup)
	 */
	ESTABLISHED,
} state = IDLE;

static void hangup(void) {
	switch(state) {
		case ESTABLISHED:
			// terminate connection
			press_key(KEY_HUP, SHORT);
			break;
		case DIALING:
			// clear dialed numbers
			press_key(KEY_C, LONG);
			dial_num = 0;
			loopcount_dial = 0;
			break;
		default:
			break;
	}
	state = IDLE;
	LED_PORT |= 1<<LED_BIT;
}

static void pickup(void) {
	if (state == IDLE) {
		state = PICKEDUP;
		LED_PORT &= ~(1<<LED_BIT);
	}
}

static void dial_number(uint8_t n) {
	switch (state) {
		case PICKEDUP:
			state = DIALING;
		case DIALING:
		case ESTABLISHED:
			while (n--) {
				LED_PORT |= 1<<LED_BIT;
				_delay_ms(40);
				LED_PORT &= ~(1<<LED_BIT);
				_delay_ms(40);
			}
			press_key(n, SHORT); // for 0-9, the enum is sorted
		default:
			break;
	}
}

static void connect(void) {
	press_key( KEY_CALL, SHORT );
	state = ESTABLISHED;
}

int main(void) {
	LED_DDR |= 1<<LED_BIT;

	HUP_DDR &= ~(1<<HUP_BIT); // input
	HUP_PORT |= 1<<HUP_BIT; // enable internal pull-up
	DIAL_DDR &= ~(1<<DIAL_BIT); // input
	DIAL_PORT |= 1<<DIAL_BIT; // enable internal pull-up
	
	for (uint8_t i=0; i<KEY_CNT; i++) {
		*(keyboard[0][i].ddr) |= 1<<keyboard[0][i].bit;
		*(keyboard[1][i].ddr) |= 1<<keyboard[1][i].bit;
	}

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
		if (state == DIALING && loopcount_dial > 50) {
			connect();
		}
		_delay_ms(25);
		loopcount_dial++;
	}
	return 0;
}
