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
	KEY_ACK,
	KEY_C,
	KEY_ASTERISK,
	KEY_SHARP,
	KEY_UP,
	KEY_DOWN,
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
	[ KEY_0 ] = { 0, 4 },
	[ KEY_1 ] = { 1, 6 },
	[ KEY_2 ] = { 1, 5 },
	[ KEY_3 ] = { 2, 3 },
	[ KEY_4 ] = { 6, 7 },
	[ KEY_5 ] = { 5, 7 },
	[ KEY_6 ] = { 4, 7 },
	[ KEY_7 ] = { 6, 8 },
	[ KEY_8 ] = { 5, 8 },
	[ KEY_9 ] = { 4, 8 },
	[ KEY_C ] = { 0, 6 },
	[ KEY_ACK ] = { 2, 5 },
	[ KEY_UP ] = { 0, 3 },
	[ KEY_DOWN ] = { 1, 3 },
	[ KEY_ASTERISK ] = { 2, 6 },
	[ KEY_SHARP ] = { 2, 4 },
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

#define PWR_DDR DDRB
#define PWR_PORT PORTB
#define PWR_BIT PB2

static void press_pwr(uint8_t duration) {
	PWR_DDR |= 1<<PWR_BIT;
	PWR_PORT &= ~(1<<PWR_BIT);
	wait(duration);
	PWR_PORT |= 1<<PWR_BIT;
	PWR_DDR &= ~(1<<PWR_BIT);
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

static void dialtone(uint8_t enabled) {
	if (enabled) {
		DDRB |= 1<<PB3;
		TCCR1A |= 1<<COM1A0;
		TCCR1B |= 1<<CS10 | 1<<WGM12;
		OCR1A = ( F_CPU / (2*425) );
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
			LED_PORT &= ~(1<<LED_BIT);
			// terminate connection
			press_key(KEY_ACK, SHORT);
			// make sure we are back at the menu
			press_key(KEY_C, SHORT);
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
			stop_bell();
			// accept phone call
			press_key(KEY_ACK, SHORT);
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
				press_pwr(LONG);
				LED_PORT &= ~(1<<LED_BIT);
			}
			break;
		case PICKEDUP:
			state = DIALING;
			dialtone(0);
		case DIALING:
		case ESTABLISHED:
			press_key(n, SHORT); // for 0-9, the enum is sorted
#if DEBUG
			while (n--) {
				LED_PORT |= 1<<LED_BIT;
				_delay_ms(40);
				LED_PORT &= ~(1<<LED_BIT);
				_delay_ms(40);
			}
#endif
		default:
			break;
	}
}

static void connect(void) {
	press_key( KEY_ACK, SHORT );
	state = ESTABLISHED;
	LED_PORT |= 1<<LED_BIT;
}

void ring_bell(void) {
	BELL_PORT &= ~(1<<BELL_BIT);
}

void stop_bell(void) {
	BELL_PORT |= 1<<BELL_BIT;
}

static uint8_t incoming_ring_counter = 0;

static void incoming_call(void) {
	state = RINGING;
	dialtone(0);
	LED_PORT |= 1<<LED_BIT;
	if (incoming_ring_counter++%20 > 10) {
		stop_bell();
	} else {
		ring_bell();
	}
}

static void incoming_ceased(void) {
	stop_bell();
	incoming_ring_counter = 0;
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

	PWR_PORT |= 1<<PWR_BIT;
	PWR_DDR &= ~(1<<PWR_BIT);


	BELL_PORT |= 1<<BELL_BIT; // PNP transistor, HIGH == on, LOW == off

	RING_DDR &= ~(1<<RING_BIT);

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
		// we query the vibration motor connector, which is pulled to HIGH
		uint8_t ringing = ( ( RING_PIN & (1<<RING_BIT) ) != 0);
		if (ringing && state != ESTABLISHED) {
			loopcount_ring = 0;
			incoming_call();
		}
		if (state == RINGING && loopcount_ring > 100) {
			incoming_ceased();
		}
		loopcount_ring++;
		loopcount_dial++;
		_delay_ms(25);
	}
	return 0;
}
