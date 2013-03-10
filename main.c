/* Parallel EEPROM programmer.
 *
 * For the ATMEGA8 and perhaps others.
 *
 * (c) 2013 Lawrence Manning. */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <avr/io.h>
#include <util/delay.h>

#define USART_BAUDRATE 9600
#define BAUD_PRESCALE (((F_CPU / (USART_BAUDRATE * 16UL))) - 1)

#define COUNTER_CLOCK 0x04
#define COUNTER_RESET 0x08
#define MEM_WE 0x10
#define MEM_CE 0x20
#define MEM_OE 0x40

#define WRITE_DELAY 10

/* Serial related */
void writechar(char c);
char readchar(void);
void writestring(char *string);
void readstring(char *string);
void saydone(void);
void debugprint(char *string);
void hello(void);

/* Counter related */
void clockcounter(unsigned int count);
void resetcounter(void);

/* Memory related */
unsigned char readmembyte(void);
int writemembyte(unsigned char w, int pagemode);
int writemempage(unsigned char *b, int pagemode);

/* GLOBALS */
int debugmode = 0;
int writedelay = WRITE_DELAY;
int echo = 0;
int counter = 0;

int main(void)
{
	/* Configure the serial port UART */
	UBRRL = BAUD_PRESCALE;
	UBRRH = (BAUD_PRESCALE >> 8);
	UCSRB = (1 << RXEN) | (1 << TXEN);   /* Turn on the transmission and reception circuitry. */

	DDRB = 0x00;	/* Assume input mode except in writes. */
	DDRD = 0xfc; 	/* bits 0 and 1 for serial, 2 for clock, 3 for reset,
			 * 4 for WE, 5 for CE and 6 for OE. 7 is unused. */

	resetcounter();

	PORTD &= ~MEM_CE;	/* CE low (for good) */
	PORTD |= MEM_OE;	/* OE high */
	PORTD |= MEM_WE;	/* WE high */
	
	_delay_ms(10);		/* Lets the UART fully wake up */
	hello();

	while (1)
	{
		int badcommand = 0;
		char serialinput[64];
		char serialoutput[64];
		int c;
		
		readstring(serialinput);
		
		char *args[10];
		int argc = 0;

		/* Cut the input ito arguments. */
		args[argc++] = strtok(serialinput, " ");
		while ((args[argc++] = strtok(NULL, " ")));
		args[argc] = NULL;
		argc--; 
		
		for (c = 0; c < argc; c++)
		{
			sprintf(serialoutput, "Arg %d = %s\r\n", c, args[c]);
			debugprint(serialoutput);
		}
		
		/* Assume the first argument is "count" and set it to one. */
		int count = 1;
		if (argc > 1)
			count = strtol(args[1], NULL, 0);

		if (strcmp(args[0], "debug") == 0)
			debugmode = count;
		else if (strcmp(args[0], "echo") == 0)
			echo = count;
		else if (strcmp(args[0], "writedelay") == 0)
			writedelay = count;
		else if (strcmp(args[0], "clock") == 0)
			clockcounter(count);
		else if (strcmp(args[0], "reset") == 0)
			resetcounter();
		else if (strcmp(args[0], "dumphex") == 0)
		{
			for (c = 0; c < count; c++)
			{
				unsigned char m = readmembyte();
				unsigned char n = m;
				if (!(c % 8))
				{
					if (c != 0) writestring("\r\n");

					sprintf(serialoutput, "%04x ", c);
					writestring(serialoutput);
				}
				/* First and last printable chars. */
				if (m < ' ' || m > '~')
					m = '.';
				sprintf(serialoutput, "%02x (%c) ", n, m);
				writestring(serialoutput);
				clockcounter(1);
			}
			writestring("\r\n");
		}
		else if (strcmp(args[0], "dumptext") == 0)
		{
			for (c = 0; c < count; c++)
			{
				unsigned char m = readmembyte();
				if (m == '\n')
					writestring("\r\n");
				else if (m >= ' ' && m <= '~')
					writechar(m);
				clockcounter(1);
			}
		}
		else if (strcmp(args[0], "dumpraw") == 0)
		{
			for (c = 0; c < count; c++)
			{
				writechar(readmembyte());
				clockcounter(1);
			}
		}
		else if (strcmp(args[0], "writemembytes") == 0)
		{
			unsigned char w = 0; /* The byte to write. */
			if (argc > 2)
				w = strtol(args[2], NULL, 0);
			for (c = 0; c < count; c++)
			{
				writemembyte(w, 0);
				clockcounter(1);
			}
		}
		else if (strcmp(args[0], "testwrites") == 0)
		{
			unsigned char r[64];
			for (c = 0; c < 64; c++)
				r[c] = c;
				
			int pagemode = 1; /* Default to page writing. */
			if (argc > 2)
				pagemode = atol(args[2]);
			
			for (c = 0; c < count; c++)
			{
				if (!(writemempage(r, pagemode)))
				{
					writestring("Error: Not on a 64byte boundary.\r\n");
					break;
				}
			}
		}
		else if (strcmp(args[0], "upblock") == 0)
		{
			int pagemode = 1; /* Default to page writing. */
			if (argc > 2)
				pagemode = atol(args[2]);
			
			for (c = 0; c < count; c++)
			{
				unsigned char rawserialinput[64];
				for (c = 0; c < 64; c++)
					rawserialinput[c] = readchar();
	
				if (!(writemempage(rawserialinput, pagemode)))
				{
					writestring("Error: Not on a 64byte boundary.\r\n");
					break;
				}
			}
		}
		else if (strcmp(args[0], "getcount") == 0)
		{
			sprintf(serialoutput, "Count: %04x (%d)\r\n", counter, counter);
			writestring(serialoutput);
		}
		else
		{
			writestring("Error: Bad command\r\n");
			badcommand = 1;
		}
		
		if (!badcommand) saydone();
	}
	
	return 0; /* Never reached. */
}

void writechar(char c)
{
	while(!(UCSRA & (1<<UDRE)));

	UDR = c;
}

char readchar(void)
{
	char x;

	/* Will block until there is a char, no interrupts here. */
	while(!(UCSRA & (1<<RXC)));

	x = UDR;

	return x;
}


void writestring(char *string)
{
	char *p = string;
	
	while (*p)
	{
		writechar(*p);
		p++;
	}
}

void readstring(char *string)
{
	char x;
	char *p = string;
	
	while ((x = readchar()) != '\r')
	{
		if (echo) writechar(x);

		if (x != '\r' && x != '\n')
		{
			*p = x;
			p++;
		}
	}
	*p = '\0';
	if (echo) writestring("\r\n");
	debugprint("Got: [");
	debugprint(string);
	debugprint("]\r\n");
	
}

void saydone(void)
{
	writestring("Done\r\n");
}

void debugprint(char *string)
{
	if (debugmode) writestring(string);
}

void hello(void)
{
	writestring("EEPROM burner v0.2 READY\r\n");
}

void clockcounter(unsigned int count)
{
	int c;

	/* Up and down as quick as you can. */
	for (c = 0; c < count; c++)
	{
		PORTD &= ~COUNTER_CLOCK;
		PORTD |= COUNTER_CLOCK;
	}
	
	counter += count;
}

void resetcounter(void)
{
	/* Up and down. */
	PORTD &= ~(COUNTER_RESET | COUNTER_CLOCK);
	PORTD |= (COUNTER_RESET | COUNTER_CLOCK);
	
	counter = 0;
}

unsigned char readmembyte(void)
{
	unsigned char r;
	
	PORTD &= ~(MEM_OE);	/* OE low. */
	_delay_us(1);
	r = PINB;
	PORTD |= (MEM_OE);	/* OE high. */

	return r;
}

int writemembyte(unsigned char w, int pagemode)
{
	/* For non page mode, we need to switch the databus to output now. */
	if (!pagemode)
		DDRB = 0xff;

	PORTD &= ~MEM_WE;		/* WE low. */
	_delay_us(1);
	PORTB = w;			/* Set data. */

	PORTD |= MEM_WE;		/* WE up again. */

	if (!pagemode)
		_delay_ms(writedelay);
	else
		_delay_us(1);

	/* Back to input. */
	if (!pagemode)
		DDRB = 0x00;

	return 1;
}

int writemempage(unsigned char *b, int pagemode)
{
	int c;

	if (counter & 0x3f) return 0; 	/* Not at the start of a page. */
	
	DDRB = 0xff; /* Databus for output. */
	
	for (c = 0; c < 64; c++)
	{
		writemembyte(b[c], pagemode);

		clockcounter(1);
	}

	_delay_ms(writedelay);

	DDRB = 0x00; /* Back to input. */

	return 1;
}
