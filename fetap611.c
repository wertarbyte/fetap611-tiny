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
	KEY_ASTERISK,
	KEY_RIGHT,
	KEY_CNT
};

static struct km_con A = { &DDRD, &PORTD, PD2 };
static struct km_con B = { &DDRD, &PORTD, PD3 };
static struct km_con C = { &DDRA, &PORTA, PA0 };
static struct km_con D = { &DDRD, &PORTD, PD4 };
static struct km_con E = { &DDRD, &PORTD, PD0 };
static struct km_con F = { &DDRD, &PORTD, PD1 };
static struct km_con G = { &DDRD, &PORTD, PD6 };
static struct km_con H = { &DDRA, &PORTA, PA1 };
static struct km_con I = { &DDRD, &PORTD, PD5 };

static const struct km_con *keyboard[][2] = {
	[ KEY_0 ] = { &G, &H },
	[ KEY_1 ] = { &F, &H },
	[ KEY_2 ] = { &E, &F },
	[ KEY_3 ] = { &F, &I },
	[ KEY_4 ] = { &B, &H },
	[ KEY_5 ] = { &E, &H },
	[ KEY_6 ] = { &H, &I },
	[ KEY_7 ] = { &B, &G },
	[ KEY_8 ] = { &E, &G },
	[ KEY_9 ] = { &G, &I },
	[ KEY_C ] = { &B, &I },
	[ KEY_CALL ] = { &A, &B },
	[ KEY_HUP ] = { &C, &D },
	[ KEY_ASTERISK ] = { &F, &G },
	[ KEY_RIGHT ] = { &B, &E},
};

static void wait(uint8_t cs) {
	while (cs--) {
		_delay_ms(10);
	}
}

#define SHORT 30
#define LONG 255

static void press_key(enum key k, uint8_t duration) {
	*(keyboard[k][0]->port) |= 1<<keyboard[k][0]->bit;
	*(keyboard[k][1]->port) |= 1<<keyboard[k][1]->bit;
	wait(duration);
	*(keyboard[k][0]->port) &= ~(1<<keyboard[k][0]->bit);
	*(keyboard[k][1]->port) &= ~(1<<keyboard[k][1]->bit);
	wait(1);
}

#define BELL_DDR DDRB
#define BELL_PORT PORTB
#define BELL_BIT PB0

#define RING_DDR DDRB
#define RING_PORT PORTB
#define RING_PIN PINB
#define RING_BIT PB1

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
		case ESTABLISHED:
			// terminate connection
			press_key(KEY_HUP, SHORT);
		default:
			state = IDLE;
	}
}

static void pickup(void) {
	switch(state) {
		case IDLE:
			state = PICKEDUP;
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
				press_key( KEY_HUP, LONG*2 );
				LED_PORT &= ~(1<<LED_BIT);
			}
			break;
		case PICKEDUP:
			state = DIALING;
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
	
	for (uint8_t i=0; i<KEY_CNT; i++) {
		*(keyboard[i][0]->ddr) |= 1<<keyboard[i][0]->bit;
		*(keyboard[i][1]->ddr) |= 1<<keyboard[i][1]->bit;
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
		if (state == DIALING && loopcount_dial > 200) {
			connect();
		}
		uint8_t ringing = ( ( RING_PIN & (1<<RING_BIT) ) != 0);
		if (ringing && state != ESTABLISHED) {
			loopcount_ring = 0;
			incoming_call();
		}
		if (state == RINGING && loopcount_ring > 50) {
			incoming_ceased();
		}
		loopcount_ring++;
		loopcount_dial++;
		_delay_ms(25);
	}
	return 0;
}
