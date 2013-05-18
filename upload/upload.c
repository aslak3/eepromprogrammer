#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/stat.h>

#define REPLY_SIZE 256

void stripnl(char *s);
void checkfordone(char *command, char *replybuffer);
FILE *openserialport(char *portdevice);
void closeserialport(FILE *serialfp);
int uploadfile(FILE *serialfp, char *filename);
int readrawbytes(FILE *serialfp, int readrawcount, int offset);
void usage(char *argv0);

int main(int argc, char *argv[])
{
	char *argv0 = argv[0];
	char *serialport = "/dev/ttyUSB0";
	char *filename = NULL;
	int readrawcount = 0;
	int offset = 0;
	int c;
	
	while ((c = getopt(argc, argv, "hs:f:r:o:")) != -1)
	{
		switch(c)
		{
			case 'h':
				usage(argv[0]);
				break;
			case 's':
				serialport = optarg;
				break;
			case 'f':
				filename = optarg;
				break;
			case 'r':
				readrawcount = atol(optarg);
				break;
			case 'o':
				offset = atol(optarg);
				break;
				
			default:
				abort();
		}
	}

	FILE *serialfp = openserialport(serialport);
	if (!serialfp)
	{
		perror("Unable to open serial port");
		exit(1);
	}
	
	if (filename)
		uploadfile(serialfp, filename);
	else if (readrawcount)
		readrawbytes(serialfp, readrawcount, offset);
	else
	{
		fprintf(stderr, "No filename specified\n");
		usage(argv0);
	}
	
	closeserialport(serialfp);
	
	return (0);
}

void stripnl(char *s)
{
	char *t = s;

	while (*t != '\r' && *t != '\n') t++;
	*t = '\0';
}

void checkfordone(char *command, char *replybuffer)
{
	if (strcmp(replybuffer, "Done") != 0)
	{
		fprintf(stderr, "Invalid response to %s command (%s)\n", command, replybuffer);
		exit(1);
	}
}

FILE *openserialport(char *portdevice)
{
	int serialfd = open(portdevice, O_RDWR | O_NOCTTY | O_NDELAY);
	if (serialfd < 0)
		return NULL;

	fcntl(serialfd, F_SETFL, 0);

	struct termios options;

	tcgetattr(serialfd, &options);
	cfsetispeed(&options, B9600);
	cfsetospeed(&options, B9600);
	options.c_cflag |= CS8;
	tcsetattr(serialfd, TCSANOW, &options);

	FILE *serialfp = fdopen(serialfd, "rw+");;

	return serialfp;
}

void closeserialport(FILE *serialfp)
{
	fclose(serialfp);
}

int uploadfile(FILE *serialfp, char *filename)
{
	char replybuffer[REPLY_SIZE];

	int filefd = open(filename, O_RDONLY);
	if (filefd < 0)
	{
		perror("Unable to open input file");
		return 1;
	}
	
	struct stat statbuf;
	fstat(filefd, &statbuf);
	off_t filesize = statbuf.st_size;
	
	printf("File is %ld bytes\n", filesize);

	fprintf(serialfp, "reset\r");
	fgets(replybuffer, REPLY_SIZE, serialfp);
	stripnl(replybuffer);
	checkfordone("reset", replybuffer);
	
	printf("Writing...\n");

	off_t bytessent = 0; int blockssent = 0;
	unsigned char transmissionbuffer[64];

	while (bytessent < filesize)
	{
		int bytesread;

		memset(transmissionbuffer, 0, 64);
		fprintf(serialfp, "upblock 1\r");
		if ((bytesread = read(filefd, transmissionbuffer, 64)) < 0)
		{
			perror("Unable to read from input file");
			return 1;
		}
		fwrite(transmissionbuffer, 64, 1, serialfp);
		fgets(replybuffer, 100, serialfp);
		stripnl(replybuffer);

		blockssent++;
		bytessent += bytesread;
		printf("#");
	}
	
	printf("\n=== Sent %ld bytes in %d blocks ===\n", bytessent, blockssent);
	printf("Validating...\n");

	fprintf(serialfp, "reset\r");
	fgets(replybuffer, REPLY_SIZE, serialfp);
	stripnl(replybuffer);
	checkfordone("reset", replybuffer);

	/* Move input file FD back to the start. */
	lseek(filefd, 0, SEEK_SET);
	
	off_t bytesvalidated = 0;
	fprintf(serialfp, "dumpraw %ld\r", filesize);
	while (bytesvalidated < filesize)
	{
		unsigned char filebyte;
		unsigned char memorybyte;
		
		if (fread(&memorybyte, 1, 1, serialfp) < 1)
		{
			perror("Unable to read serial when validating");
			return 1;
		}
		
		if (read(filefd, &filebyte, 1) < 1)
		{
			perror("Unable to read file when validating");
			return 1;
		}
		
		if (memorybyte != filebyte)
		{
			printf("\n");
			fprintf(stderr, "Error validating at byte %ld (%04lx), should be %02x but got %02x\n",
				bytesvalidated, bytesvalidated, filebyte, memorybyte);
			return 1;
		}
		
		bytesvalidated++;
		
		if (!(bytesvalidated % 64))
			printf("#");
	}
	if (bytesvalidated % 64)
		printf("#");

	fgets(replybuffer, REPLY_SIZE, serialfp);
	stripnl(replybuffer);
	checkfordone("dumpraw", replybuffer);

	printf("\n=== No errors ===\n");

	if (filefd > -1)
		close(filefd);

	return 0;
}

int readrawbytes(FILE *serialfp, int readrawcount, int offset)
{
	char replybuffer[REPLY_SIZE];
	int bytesread = 0;

	fprintf(serialfp, "reset\r");
	fgets(replybuffer, REPLY_SIZE, serialfp);
	stripnl(replybuffer);
	checkfordone("reset", replybuffer);

	fprintf(serialfp, "clock %d\r", offset);
	fgets(replybuffer, REPLY_SIZE, serialfp);
	stripnl(replybuffer);
	checkfordone("clock", replybuffer);	
	
	fprintf(serialfp, "dumpraw %d\r", readrawcount);
	while (bytesread < readrawcount)
	{
		unsigned char memorybyte;
		
		if (fread(&memorybyte, 1, 1, serialfp) < 1)
		{
			perror("Unable to read serial when validating");
			return 1;
		}
		
		fwrite(&memorybyte, 1, 1, stdout);

		bytesread++;
	}

	return 0;
}

void usage(char *argv0)
{
	fprintf(stderr, "Usage: %s (-r size [-o offset])|-f filename [-s serialport]\n" \
		"\tserialport defaults to /dev/ttyUSB0\n", argv0);
	exit(1);
}
