/* I2C EEPROM programmer.
 *
 * For the ATMEGA8 and perhaps others.
 *
 * (c) 2013 Lawrence Manning. */

#define BUFFER_SIZE 50
#define PAGE_SIZE 64

/* For debugging */
void writechar(char c, unsigned char debug);
void writehexbyte(unsigned char b, unsigned char debug);
void writehexword(unsigned int n, unsigned char debug);
void writestring(char *string, unsigned char debug);

/* Functions to implement. */

/* General */
void init(void);

/* Counter related */
void clockcounter(unsigned int count);
void resetcounter(void);

/* Memory related */
void resetmemory(void);
int startreadmembyte(void);
int nextreadmembyte(unsigned char *r, int last);
int writemembyte(unsigned char w);
int writemempage(unsigned char *b);
unsigned char memoryfailed(void);

/* Globals to declare. */

extern char *greeting;
extern int counter;
extern int writedelay;
