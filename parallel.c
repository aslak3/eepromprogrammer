/* Parallel EEPROM programmer.
 *
 * For the ATMEGA8 and perhaps others.
 *
 * (c) 2013 Lawrence Manning. */

/* Parallel backend. */

#include <avr/io.h>
#include <util/delay.h>

#include "programmer.h"

#define COUNTER_CLOCK 0x04
#define COUNTER_RESET 0x08
#define MEM_WE 0x10
#define MEM_CE 0x20
#define MEM_OE 0x40

char *greeting = "EEPROM burner v0.4\r\n";
int writedelay = 10;

void init(void)
{
	DDRB = 0x00;    /* Assume input mode except in writes. */
	DDRD = 0xfc;    /* bits 0 and 1 for serial, 2 for clock, 3 for reset,
	                 * 4 for WE, 5 for CE and 6 for OE. 7 is unused. */
}

void clockcounter(unsigned int count)
{
	int c;

	/* Up and down as quick as you can. */
	for (c = 0; c < count; c++)
	{
		PORTD &= ~COUNTER_CLOCK;
		_delay_us(10);
		PORTD |= COUNTER_CLOCK;
		_delay_us(10);
	}
	
	counter += count;
}

void resetcounter(void)
{
	/* Up and down. */
	PORTD &= ~(COUNTER_RESET | COUNTER_CLOCK);
	_delay_us(10);
	PORTD |= (COUNTER_RESET | COUNTER_CLOCK);
	_delay_us(10);
	
	counter = 0;
}

void resetmemory(void)
{
	PORTD &= ~MEM_CE;       /* CE low (for good) */
	PORTD |= MEM_OE;        /* OE high */
	PORTD |= MEM_WE;        /* WE high */
}

int startreadmembyte(void)
{
	return 1;
}

int nextreadmembyte(unsigned char *r, int last)
{
	PORTD &= ~(MEM_OE);	/* OE low. */
	_delay_us(10);
	*r = PINB;
	PORTD |= (MEM_OE);	/* OE high. */
	_delay_us(10);

	return 1;
}

int writemembyte(unsigned char w, unsigned char pagemode)
{
	PORTD &= ~MEM_WE;		/* WE low. */
	_delay_us(10);
	PORTB = w;			/* Set data. */
	_delay_us(10);

	PORTD |= MEM_WE;		/* WE up again. */
	_delay_us(10);
	
	if (!pagemode) delayforwrite();
		
	return 1;
}

int writemempage(unsigned char *b)
{
	int c;

	if (counter & 0x3f) return 0; 	/* Not at the start of a page. */
	
	DDRB = 0xff; /* Databus for output. */
	
	for (c = 0; c < PAGE_SIZE; c++)
	{
		writemembyte(b[c], 1);

		clockcounter(1);
	}

	delayforwrite();

	DDRB = 0x00; /* Back to input. */

	return 1;
}

unsigned char memoryfailed(void)
{
	return 0;	
}
