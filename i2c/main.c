/* I2C EEPROM programmer.
 *
 * For the ATMEGA8 and perhaps others.
 *
 * (c) 2013 Lawrence Manning. */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <avr/io.h>
#include <util/delay.h>
#include <util/twi.h>

#define USART_BAUDRATE 9600
#define BAUD_PRESCALE (((F_CPU / (USART_BAUDRATE * 16UL))) - 1)

#define EEDEVADR 0

#define BUFFER_SIZE 64

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

/* TWI related */
void resettwi(void);
void starttwi(void);
void stoptwi(void);
void writetwi(unsigned char data);
unsigned char readtwi(int ack);
unsigned char statustwi(void);
int sendaddress(void);
void twifailed(void);

/* Memory related */
int startreadmembyte(void);
int nextreadmembyte(unsigned char *r, int last);
int writemembyte(unsigned char w);
int writemempage(unsigned char *b);

/* GLOBALS */
int debugmode = 0;
int echo = 0;
int counter = 0;
unsigned char lasttwierror = 0x00;

int main(void)
{
	/* Configure the serial port UART */
	UBRRL = BAUD_PRESCALE;
	UBRRH = (BAUD_PRESCALE >> 8);
	UCSRC = (1 << URSEL)|(3 << UCSZ0);
	UCSRB = (1 << RXEN) | (1 << TXEN);   /* Turn on the transmission and reception circuitry. */
	
	resetcounter();

	resettwi();

	_delay_ms(10);		/* Lets the UART fully wake up */
	hello();

	while (1)
	{
		int badcommand = 0;
		char serialinput[BUFFER_SIZE];
		char serialoutput[BUFFER_SIZE];
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
		else if (strcmp(args[0], "clock") == 0)
			clockcounter(count);
		else if (strcmp(args[0], "reset") == 0)
			resetcounter();
		else if (strcmp(args[0], "dumphex") == 0)
		{
			if (!(startreadmembyte()))
			{
				twifailed();
				continue;
			}
			for (c = 0; c < count; c++)
			{
				unsigned char m = 0;
				if (!(nextreadmembyte(&m, c == count - 1)))
				{
					twifailed();
					break;
				}
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
			if (!(startreadmembyte()))
			{
				twifailed();
				continue;
			}
			for (c = 0; c < count; c++)
			{
				unsigned char m;
				if (!(nextreadmembyte(&m, c == count - 1)))
				{
					twifailed();
					break;
				}
				if (m == '\n')
					writestring("\r\n");
				else if (m >= ' ' && m <= '~')
					writechar(m);
				clockcounter(1);
			}
		}
		else if (strcmp(args[0], "dumpraw") == 0)
		{
			startreadmembyte();
			for (c = 0; c < count; c++)
			{
				unsigned char m;
				nextreadmembyte(&m, c == count - 1);
				writechar(m);
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
					twifailed();
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

			unsigned char r[BUFFER_SIZE];
			for (c = 0; c < BUFFER_SIZE; c++)
				r[c] = c ^ neg;
				
			for (c = 0; c < count; c++)
			{
				if (!(writemempage(r)))
				{
					twifailed();
					break;
				}
			}
		}
		else if (strcmp(args[0], "testreads") == 0)
		{
			if (!(startreadmembyte()))
			{
				twifailed();
				continue;
			}
			unsigned char neg = 0;
			if (argc > 2)
				neg = strtol(args[2], NULL, 0);

			int seenpage = -1;
			for (c = 0; c < count; c++)
			{
				int d;
				for (d = 0; d < BUFFER_SIZE; d++)
				{
					unsigned char m;
					nextreadmembyte(&m, (d == BUFFER_SIZE - 1) &&
						(c == count - 1));
					if (m != (d ^ neg))
					{
						if (c != seenpage)
						{
							sprintf(serialoutput, "Error: Failed %d (page %d byte %d)\r\n", (c * BUFFER_SIZE) + d, c, d);
							writestring(serialoutput);
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
				unsigned char rawserialinput[BUFFER_SIZE];
				for (c = 0; c < BUFFER_SIZE; c++)
					rawserialinput[c] = readchar();
	
				if (!(writemempage(rawserialinput)))
				{
					twifailed();
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
	while(!(UCSRA & (1 << RXC)));

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
	writestring("I2C EEPROM burner v0.1 READY\r\n");
}

void clockcounter(unsigned int count)
{
	counter += count;
}

void resetcounter(void)
{
	counter = 0;
}

void resettwi(void)
{
	/* Set SCL to 400kHz. */
	TWSR = 0x00;
	TWBR = 0x0C;
	/* Enable TWI. */
	TWCR = (1 << TWEN);
	
	debugprint("TWI reset\r\n");
}

void starttwi(void)
{
	debugprint("Starting twi\r\n");

	TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
	while (!(TWCR & (1 << TWINT)))

	debugprint("TWI started\r\n");
}

void stoptwi(void)
{
	TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
	debugprint("TWI stopped\r\n");
}

void writetwi(unsigned char data)
{
	char buffer[BUFFER_SIZE];
	snprintf(buffer, BUFFER_SIZE, "Writting to TWI: %02x\r\n", data);
	debugprint(buffer);
	
	TWDR = data;
	TWCR = (1 << TWINT) | (1 << TWEN);
	while ((TWCR & (1 << TWINT)) == 0);

	debugprint("Finished writting to TWI\r\n");	
}

unsigned char readtwi(int ack)
{
	debugprint("Reading from TWI\r\n");

	/* See if we need to ACK. */
	TWCR = (1 << TWINT) | (1 << TWEN) | (ack ? (1 << TWEA) : 0);
	while ((TWCR & (1 << TWINT)) == 0);
	unsigned char d = TWDR;
	
	char buffer[BUFFER_SIZE];
	snprintf(buffer, BUFFER_SIZE, "Got from TWI: %02x\r\n", d);
	debugprint(buffer);
	return d;
}

unsigned char statustwi(void)
{
	unsigned char status = TWSR & 0xF8;
	lasttwierror = status;
	
	char buffer[BUFFER_SIZE];
	snprintf(buffer, BUFFER_SIZE, "TWI status: %02x\r\n", status);
	debugprint(buffer);

	return status;
}

void twifailed(void)
{
	char buffer[BUFFER_SIZE];
	snprintf(buffer, BUFFER_SIZE, "Failed with status %02x\r\n", lasttwierror);
	writestring(buffer);
}

int sendaddress(void)
{
	char buffer[BUFFER_SIZE];
	snprintf(buffer, BUFFER_SIZE, "Sending TWI address: %d\r\n", counter);
	debugprint(buffer);
	
	if (statustwi() != TW_START) return 0;
	writetwi(0xa0|EEDEVADR);
	if (statustwi() != TW_MT_SLA_ACK) return 0;
	writetwi((unsigned char)((counter & 0xff00) >> 8));
	if (statustwi() != TW_MT_DATA_ACK) return 0;
	writetwi((unsigned char)(counter & 0x00ff));
	if (statustwi() != TW_MT_DATA_ACK) return 0;
	
	return 1;
}

int startreadmembyte(void)
{	
	starttwi();
	if (!sendaddress()) goto BAD;
	/* Start again for the data returned. */
	starttwi();
	if (statustwi() != TW_REP_START) goto BAD;
	/* 0xa0 is %1010 ... the magic "address" for the 24LC256. */
	writetwi(0xa0 | EEDEVADR | 0x01);
	if (statustwi() != TW_MR_SLA_ACK) goto BAD;
	return 1;

BAD:
	stoptwi();
	return 0;
}

int nextreadmembyte(unsigned char *r, int last)
{	
	/* For the last byte, don't ACK it and do a stop. */
	if (last)
	{
		*r = readtwi(0);
		if (statustwi() != TW_MR_DATA_NACK) goto BAD;
		stoptwi();
	}
	else
	{
		*r = readtwi(1);
		if (statustwi() != TW_MR_DATA_ACK) goto BAD;
	}
	return 1;

BAD:
	stoptwi();
	return 0;
}

int writemembyte(unsigned char w)
{
	starttwi();
	if (!sendaddress()) goto BAD;
	writetwi(w);
	if (statustwi() != TW_MT_DATA_ACK) goto BAD;
	stoptwi();
	
	/* Delay while write completes. */
	_delay_ms(5);
	
	return 1;

BAD:
	stoptwi();
	return 0;
}

/* Writes a 64byte page. */
int writemempage(unsigned char *b)
{
	if (counter & 0x3f)
	{
		lasttwierror = 0xff; /* Special error code. */
		return 0;   /* Not at the start of a page. */
	}
	
	starttwi();
	if (!(sendaddress())) goto BAD;
	
	int c;
	for (c = 0; c < BUFFER_SIZE; c++)
	{
		writetwi(b[c]);
		if (statustwi() != TW_MT_DATA_ACK) goto BAD;
		clockcounter(1);
	}
	stoptwi();
	
	/* Delay while write completes. */
	_delay_ms(5);

	return 1;
BAD:
	stoptwi();
	return 0;
}
