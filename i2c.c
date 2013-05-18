/* I2C EEPROM programmer.
 *
 * For the ATMEGA8 and perhaps others.
 *
 * (c) 2013 Lawrence Manning. */

/* I2C backend */

#include <util/delay.h>
#include <util/twi.h>

#include "programmer.h"

#define EEDEVADR 0

/* TWI related */
static void resettwi(void);
static void starttwi(void);
static void stoptwi(void);
static void writetwi(unsigned char data);
static unsigned char readtwi(int ack);
static unsigned char statustwi(void);
static int sendaddress(void);

unsigned char lasttwierror = 0;

/* EXPORTED GLOBALS */
char *greeting = "I2C EEPROM burner v0.1\r\n";
int writedelay = 5;

/* LOCAL GLOBALS */
char buffer[BUFFER_SIZE];

void init(void)
{
}

void clockcounter(unsigned int count)
{
	counter += count;
}

void resetcounter(void)
{
	counter = 0;
}

void resetmemory(void)
{
	resettwi();
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
	_delay_ms(writedelay);
	
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

	unsigned char c;
	for (c = 0; c < PAGE_SIZE; c++)
	{
		writetwi(b[c]);
		if (statustwi() != TW_MT_DATA_ACK) goto BAD;
		clockcounter(1);
	}
	stoptwi();
	
	/* Delay while write completes. */
	_delay_ms(writedelay);

	return 1;
BAD:
	stoptwi();
	return 0;
}

unsigned char memoryfailed(void)
{
	return lasttwierror;
}

static void resettwi(void)
{
	/* Set SCL to 400kHz. */
	TWSR = 0x00;
	TWBR = 0x0C;
	/* Enable TWI. */
	TWCR = (1 << TWEN);
}

static void starttwi(void)
{
	TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
	while (!(TWCR & (1 << TWINT)));

	writestring("TWI started\r\n", 1);
}

static void stoptwi(void)
{
	TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);

	writestring("TWI stopped\r\n", 1);
}

static void writetwi(unsigned char data)
{
	TWDR = data;
	TWCR = (1 << TWINT) | (1 << TWEN);
	while ((TWCR & (1 << TWINT)) == 0);

	writestring("Written to TWI: ", 1);
	writehexbyte(data, 1);
	writestring("\r\n", 1);
}

static unsigned char readtwi(int ack)
{
	/* See if we need to ACK. */
	TWCR = (1 << TWINT) | (1 << TWEN) | (ack ? (1 << TWEA) : 0);
	while ((TWCR & (1 << TWINT)) == 0);
	unsigned char d = TWDR;
	
	writestring("Read from TWI:", 1);
	writehexbyte(d, 1);
	writestring("\r\n", 1);

	return d;
}

static unsigned char statustwi(void)
{
	lasttwierror = TWSR & 0xF8;

	return lasttwierror;
}

static int sendaddress(void)
{
	if (statustwi() != TW_START) return 0;
	writetwi(0xa0|EEDEVADR);
	if (statustwi() != TW_MT_SLA_ACK) return 0;
	writetwi((unsigned char)((counter & 0xff00) >> 8));
	if (statustwi() != TW_MT_DATA_ACK) return 0;
	writetwi((unsigned char)(counter & 0x00ff));
	if (statustwi() != TW_MT_DATA_ACK) return 0;
	
	return 1;
}
