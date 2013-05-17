/* EEPROM programmer, common code.
 *
 * For the ATMEGA8 and perhaps others.
 *
 * (c) 2013 Lawrence Manning. */

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include <avr/io.h>
#include <util/delay.h>

#define USART_BAUDRATE 9600
#define BAUD_PRESCALE (((F_CPU / (USART_BAUDRATE * 16UL))) - 1)

#include "programmer.h"

/* Serial related */
void writechar(char c, unsigned char debug);
void writehexbyte(unsigned char b, unsigned char debug);
void writehexword(unsigned int n, unsigned char debug);
void writestring(char *string, unsigned char debug);
char readchar(void);
void readstring(char *string);
void hello(void);

/* EXPORTED GLOBALS */
unsigned char debugmode = 0;
unsigned char echo = 0;
int counter = 0;

int main(void)
{
	/* Configure the serial port UART */
	UBRRL = BAUD_PRESCALE;
	UBRRH = (BAUD_PRESCALE >> 8);
	UCSRC = (1 << URSEL) | (3 << UCSZ0);
	UCSRB = (1 << RXEN) | (1 << TXEN);   /* Turn on the transmission and reception circuitry. */
	
	init();
	resetcounter();
	resetmemory();

	_delay_ms(10);		/* Lets the UART fully wake up */
	hello();

	for (;;)
	{
		int c;
		unsigned char page[PAGE_SIZE];
		unsigned char erroredcommand = 0;
		char serialinput[BUFFER_SIZE];
		
		readstring(serialinput);
		
		char *args[10];
		unsigned char argc = 0;

		/* Cut the input ito arguments. */
		args[argc++] = strtok(serialinput, " ");
		while ((args[argc++] = strtok(NULL, " ")));
		args[argc] = NULL;
		--argc; 
		
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
			if (!(startreadmembyte()))
				erroredcommand = 1;            
			else
			{
				for (c = 0; c < count; c++)
				{
					unsigned char m = 0;
					if (!(nextreadmembyte(&m, c == count - 1)))
					{
						erroredcommand = 1;
						break;
					}
					unsigned char n = m;
					if (!(c % 8))
					{
						if (c != 0) writestring("\r\n", 0);
						writehexword(c, 0);
						writechar(' ', 0);
					}
					/* First and last printable chars. */
					if (m < ' ' || m > '~')
						m = '.';
					writehexbyte(n, 0);
					writestring(" (", 0);
					writechar(m, 0);
					writestring(") ", 0);
					clockcounter(1);
				}
				writestring("\r\n", 0);
			}
		}
		else if (strcmp(args[0], "dumptext") == 0)
		{
			if (!(startreadmembyte()))
				erroredcommand = 1;
			else
			{
				for (c = 0; c < count; c++)
				{
					unsigned char m;
					if (!(nextreadmembyte(&m, c == count - 1)))
					{
						erroredcommand = 1;
						break;
					}
					if (m == '\n')
						writestring("\r\n", 0);
					else if (m >= ' ' && m <= '~')
						writechar(m, 0);
					clockcounter(1);
				}
			}
		}
		else if (strcmp(args[0], "dumpraw") == 0)
		{
			startreadmembyte();
			for (c = 0; c < count; c++)
			{
				unsigned char m;
				nextreadmembyte(&m, c == count - 1);
				writechar(m, 0);
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
				if (!(writemembyte(w)))
				{
					erroredcommand = 1;
					break;
				}
				clockcounter(1);
			}
		}
		else if (strcmp(args[0], "testwrites") == 0)
		{
			unsigned char neg = 0;
			if (argc > 2)
				neg = strtol(args[2], NULL, 0);

			for (c = 0; c < PAGE_SIZE; c++)
				page[c] = c ^ neg;
				
			for (c = 0; c < count; c++)
			{
				if (!(writemempage(page)))
				{
					erroredcommand = 1;
					break;
				}
			}
		}
		else if (strcmp(args[0], "testreads") == 0)
		{
			startreadmembyte();
			unsigned char neg = 0;
			if (argc > 2)
				neg = strtol(args[2], NULL, 0);

			unsigned char seenpage = -1;
			for (c = 0; c < count; c++)
			{
				unsigned char d;
				for (d = 0; d < PAGE_SIZE; d++)
				{
					unsigned char m;
					nextreadmembyte(&m, (d == PAGE_SIZE - 1) &&
						(c == count - 1));
					if (m != (d ^ neg))
					{
						if (c != seenpage)
						{
							writestring("Error: Failed ", 0);
							writehexword((c * PAGE_SIZE) + d, 0);
							writestring(" (page ", 0);
							writehexbyte(c, 0);
							writestring(" byte ", 0);
							writehexbyte(d, 0);
							writestring(")\r\n", 0);

							seenpage = c;
						}
					}
					clockcounter(1);
				}
			}
		}
		else if (strcmp(args[0], "upblock") == 0)
		{
			for (c = 0; c < count; c++)
			{
				for (c = 0; c < PAGE_SIZE; c++)
					page[c] = readchar();
	
				if (!(writemempage(page)))
				{
					erroredcommand = 1;
					break;
				}
			}
		}
		else if (strcmp(args[0], "getcount") == 0)
		{
			writehexword(counter, 0);
			writestring("\r\n", 0);
		}
		else
		{
			writestring("Bad command\r\n", 0);
			continue;
		}
		
		if (erroredcommand)
		{
			writestring("Failed: ", 0);
			writehexbyte(memoryfailed(), 0);
			writestring("\r\n", 0);
		}
		else writestring("Done\r\n", 0);
	}
	
	return 0; /* Not reached. */
}

void writechar(char c, unsigned char debug)
{
	if (debug && !debugmode) return;
	
	while(!(UCSRA & (1<<UDRE)));

	UDR = c;
}

void writehexbyte(unsigned char b, unsigned char debug)
{
	char hex = (b & 0xf0) >> 4;
	hex += '0';
	if (hex > '9') hex += 'A' - '9' - 1;

	writechar(hex, debug);

	hex = b & 0x0f;
	hex += '0';
	if (hex > '9') hex += 'A' - '9' - 1;

	writechar(hex, debug);
}

void writehexword(unsigned int n, unsigned char debug)
{
	writehexbyte((n & 0xff00) >> 8, debug);
	writehexbyte(n & 0x00ff, debug);
}	


void writestring(char *string, unsigned char debug)
{
	char *p = string;
	
	while (*p)
	{
		writechar(*p, debug);
		p++;
	}
}

char readchar(void)
{
	char x;

	/* Will block until there is a char, no interrupts here. */
	while(!(UCSRA & (1 << RXC)));

	x = UDR;

	return x;
}

void readstring(char *string)
{
	char x;
	char *p = string;
	
	while ((x = readchar()) != '\r')
	{
		if (echo) writechar(x, 0);

		if (x != '\r' && x != '\n')
		{
			*p = x;
			p++;
		}
	}
	*p = '\0';
	if (echo) writestring("\r\n", 0);
}

void hello(void)
{
	writestring(greeting, 0);
}
