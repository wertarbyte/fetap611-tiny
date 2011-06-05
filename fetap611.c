#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define LED_BIT 7
#define LED_DDR DDRB
#define LED_PORT PORTB

struct km_con {
	uint8_t *ddr;
	uint8_t *port;
	uint8_t bit;
};

static enum key {
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

static uint8_t dial_num = 0;

static void hangup(void) {
	LED_PORT |= 1<<LED_BIT;
	press_key(KEY_HUP, 20);
}

static void pickup(void) {
	LED_PORT &= ~(1<<LED_BIT);
	press_key(KEY_CALL, 20);
}

static void dial_number(uint8_t n) {
	while (n--) {
		LED_PORT |= 1<<LED_BIT;
		_delay_ms(40);
		LED_PORT &= ~(1<<LED_BIT);
		_delay_ms(40);
	}
	press_key(n, 20); // for 0-9, the enum is sorted
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

	uint8_t loopcount_dial = 0;
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
		if (!st_hup) {
			uint8_t dial = ( (DIAL_PIN & (1<<DIAL_BIT)) != 0 );
			if (dial != st_dial) {
				if (dial) {
					//LED_PORT |= 1<<LED_BIT;
					dial_num++;
					loopcount_dial=0;
				} else {
					//LED_PORT &= ~(1<<LED_BIT);
				}
				st_dial = dial;
			}
			if (loopcount_dial > 10 && dial_num > 0) {
				dial_number(dial_num%10);
				dial_num = 0;
				loopcount_dial = 0;
			}
		}
		_delay_ms(25);
		loopcount_dial++;
	}
	return 0;
}
