/* I2C and parallel EEPROM programmer.
 *
 * For the ATMEGA8 and perhaps others.
 *
 * (c) 2013 Lawrence Manning. */

/* Common header. */

#define BUFFER_SIZE 50
#define PAGE_SIZE 64

/* From main.c */
void writechar(char c, unsigned char debug);
void writehexbyte(unsigned char b, unsigned char debug);
void writehexword(unsigned int n, unsigned char debug);
void writestring(char *string, unsigned char debug);
void delayforwrite(void);

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
int writemembyte(unsigned char w, unsigned char pagemode);
int writemempage(unsigned char *b);
unsigned char memoryfailed(void);
unsigned char sdpdisable(void);
unsigned char sdpenable(void);

/* Globals to declare. */

extern char *greeting;
extern int counter;
extern int writedelay;
