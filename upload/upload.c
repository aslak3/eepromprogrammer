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

int main(int argc, char *argv[])
{
	char replybuffer[REPLY_SIZE];
	char *filename = NULL;
	
	if (argc < 2)
	{
		fprintf(stderr, "Usage: %s filename\n", argv[0]);
		exit(1);
	}
	
	filename = argv[1];

	int filefd = open(filename, O_RDONLY);
	if (filefd < 0)
	{
		perror("Unable to open input file");
		exit(1);
	}
	
	struct stat statbuf;
	fstat(filefd, &statbuf);
	off_t filesize = statbuf.st_size;
	
	printf("File is %ld bytes\n", filesize);

	int serialfd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY | O_NDELAY);
	if (serialfd < 0)
	{
		perror("Unable to open serial port");
		exit(1);
	}

	fcntl(serialfd, F_SETFL, 0);

	struct termios options;

	tcgetattr(serialfd, &options);
	cfsetispeed(&options, B9600);
	cfsetospeed(&options, B9600);
	options.c_cflag |= CS8;
	tcsetattr(serialfd, TCSANOW, &options);

	FILE *serialfp = fdopen(serialfd, "rw+");
	if (!serialfp)
	{
		perror("Unable to open serial port as file handle");
		exit(1);
	}

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
		int mode = 1;
		
		memset(transmissionbuffer, 0, 64);
		if (bytessent == 8128 ||
			bytessent == 16320 ||
			bytessent == 960)
		{
			mode = 0;
		}
		fprintf(serialfp, "upblock 1 %d\r", mode);
		if ((bytesread = read(filefd, transmissionbuffer, 64)) < 0)
		{
			perror("Unable to read from input file");
			exit(1);
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
			exit(1);
		}
		
		if (read(filefd, &filebyte, 1) < 1)
		{
			perror("Unable to read file when validating");
			exit(1);
		}
		
		if (memorybyte != filebyte)
		{
			printf("\n");
			fprintf(stderr, "Error validating at byte %ld (%04lx), should be %02x but got %02x\n",
				bytesvalidated, bytesvalidated, filebyte, memorybyte);
			exit(1);
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
	
	if (serialfp)
		fclose(serialfp);
	if (serialfd > -1)
		close(serialfd);
	if (filefd > -1)
		close(filefd);
	
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
