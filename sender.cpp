#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "msg.h"    /* For the message struct */

/* The size of the shared memory chunk */
#define SHARED_MEMORY_CHUNK_SIZE 1000

/* The ids for the shared memory segment and the message queue */
int shmid, msqid;

/* The pointer to the shared memory */
void* sharedMemPtr;

/**
 * Sets up the shared memory segment and message queue
 * @param  shmid The id of the allocated shared memory
 * @param  msqid The id of the allocated message queue
 */
void init(int& shmid, int& msqid, void*& sharedMemPtr)
{
	/* Generate the key for the shared memory segment and message queue */
	key_t key = ftok("keyfile.txt", 'a');

	/* Failed to generate the key */
	if (key < 0)
	{
		perror("ftok");
		exit(-1);
	}

	/* Get the shared memory segment ID */
	shmid = shmget(key, SHARED_MEMORY_CHUNK_SIZE, S_IRUSR | S_IWUSR);

	/* Failed to get the shared memory segment ID */
	if (shmid < 0)
	{
		perror("shmget");
		exit(-1);
	}

	/* Attach to the shared memory segment */
	sharedMemPtr = shmat(shmid, NULL, 0);

	/* Failed to attach to shared memory */
	if (sharedMemPtr < 0)
	{
		perror("shmat");
		exit(-1);
	}

	/* Attach to the message queue */
	msqid = msgget(key, 0666 | IPC_CREAT);

	/* Failed to attach to the message queue */
	if (msqid < 0)
	{
		perror("msgget");
		exit(-1);
	}
}

/**
 * Performs the cleanup functions
 * @param  sharedMemPtr The pointer to the shared memory
 * @param  shmid The id of the shared memory segment
 * @param  msqid The id of the message queue
 */
void cleanUp(const int& shmid, const int& msqid, void* sharedMemPtr)
{
	/* Detach from shared memory */
	if (shmdt(sharedMemPtr) < 0)
	{
		perror("shmdt");
		exit(-1);
	}
}

/**
 * The main send function
 * @param  fileName The name of the file
 * @return The number of bytes sent
 */
unsigned long sendFile(const char* fileName)
{
	/* Open the file for reading */
	FILE* fp = fopen(fileName, "r");

	/* A buffer to store message we will send to the receiver. */
	message sndMsg;
	sndMsg.mtype = SENDER_DATA_TYPE;
	
	/* A buffer to store message received from the receiver. */
	ackMessage rcvMsg;
	
	/* The number of bytes sent */
	unsigned long numBytesSent = 0;
	
	/* Was the file open? */
	if (!fp)
	{
		perror("fopen");
		exit(-1);
	}
	
	/* Read the whole file */
	while (!feof(fp))
	{
		/* Read at most SHARED_MEMORY_CHUNK_SIZE from the file and store them in shared memory. 
 		 * fread will return how many bytes it has actually read (since the last chunk may be less
 		 * than SHARED_MEMORY_CHUNK_SIZE).
 		 */
		if ((sndMsg.size = fread(sharedMemPtr, sizeof(char), SHARED_MEMORY_CHUNK_SIZE, fp)) < 0)
		{
			perror("fread");
			exit(-1);
		}
		
		/* Count the number of bytes sent */
		numBytesSent += sndMsg.size;

		/* Send a message to the receiver that the data is ready */
		if (msgsnd(msqid, &sndMsg, sizeof(message) - sizeof(long), 0) < 0)
		{
			perror("msgsnd");
			exit(-1);
		}
		
		/* Get acknowledgment that the data has been received */
		if (msgrcv(msqid, &rcvMsg, sizeof(rcvMsg) - sizeof(long), RECV_DONE_TYPE, 0) < 0)
		{
			perror("msgrcv");
			exit(-1);
		}
	}
	
	/* Set the size of the sending message to zero to signal that there is no more data to send */
	sndMsg.size = 0;

	/* Send the message to the receiver */
	if (msgsnd(msqid, &sndMsg, sizeof(message) - sizeof(long), 0) < 0)
	{
		perror("fread");
		exit(-1);
	}

	/* Close the file */
	fclose(fp);
	
	return numBytesSent;
}

/**
 * Used to send the name of the file to the receiver
 * @param  fileName The name of the file to send
 */
void sendFileName(const char* fileName)
{
	/* Get the length of the file name */
	int fileNameSize = strlen(fileName);

	/* Validate the length of the file name */
	if (fileNameSize > MAX_FILE_NAME_SIZE)
	{
		fprintf(stderr, "File name exceeds max size of %d.\n", MAX_FILE_NAME_SIZE);
		exit(-1);
	}

	/* Create a message object for sending the filename */
	fileNameMsg msg;
	msg.mtype = FILE_NAME_TRANSFER_TYPE;
	strncpy(msg.fileName, fileName, fileNameSize + 1);

	/* Send the message using msgsnd */
	if (msgsnd(msqid, &msg, sizeof(fileNameMsg) - sizeof(long), 0) < 0)
	{
		perror("msgsnd");
		exit(-1);
	}
}

/**
 * Begins program execution
 * @param  argc The number of command line arguments
 * @param  argv An array of C strings containing each command line argument
 * @return The exit code
 */
int main(int argc, char** argv)
{
	/* Check the command line arguments */
	if (argc < 2)
	{
		fprintf(stderr, "USAGE: %s <FILE NAME>\n", argv[0]);
		exit(-1);
	}
		
	/* Connect to shared memory and the message queue */
	init(shmid, msqid, sharedMemPtr);
	
	/* Send the name of the file */
	sendFileName(argv[1]);
		
	/* Send the file */
	fprintf(stderr, "The number of bytes sent is %lu\n", sendFile(argv[1]));
	
	/* Cleanup */
	cleanUp(shmid, msqid, sharedMemPtr);
		
	return 0;
}
