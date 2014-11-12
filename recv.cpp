#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include "msg.h"    /* For the message struct */

using namespace std;

/* The size of the shared memory chunk */
#define SHARED_MEMORY_CHUNK_SIZE 1000

/* The ids for the shared memory segment and the message queue */
int shmid, msqid;

/* The pointer to the shared memory */
void *sharedMemPtr;

/**
 * The function for receiving the name of the file
 * @return The name of the file received from the sender
 */
string recvFileName()
{
	/* A message object for receiving the file name */
	fileNameMsg msg;

	/* Receive the file name using msgrcv() */
	if (msgrcv(msqid, &msg, sizeof(fileNameMsg) - sizeof(long), FILE_NAME_TRANSFER_TYPE, 0) < 0)
	{
		perror("msgrcv");
		exit(-1);
	}
	
	/* Return the received file name */
	return msg.fileName;
}

 /**
  * Sets up the shared memory segment and message queue
  * @param  shmid The id of the allocated shared memory
  * @param  msqid The id of the shared memory
  * @param  sharedMemPtr The pointer to the shared memory
  */
void init(int& shmid, int& msqid, void*& sharedMemPtr)
{
	/* Generate a key for the shared memory segment and message queue */
	key_t key = ftok("keyfile.txt", 'a');

	/* Failed to generate the key */
	if (key < 0)
	{
		perror("ftok");
		exit(-1);
	}

	/* Allocate a shared memory segment */
	shmid = shmget(key, SHARED_MEMORY_CHUNK_SIZE, IPC_CREAT | S_IRUSR | S_IWUSR);

	/* Failed to allocate shared memory */
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

	/* Create a message queue */
	msqid = msgget(key, 0666 | IPC_CREAT);

	/* Failed to create the message queue */
	if (msqid < 0)
	{
		perror("msgget");
		exit(-1);
	}
}

/**
 * The main loop
 * @param  fileName The name of the file received from the sender
 * @return The number of bytes received
 */
unsigned long mainLoop(const char* fileName)
{
	/* The size of the message received from the sender */
	int msgSize = -1;
	
	/* The number of bytes received */
	int numBytesRecv = 0;
	
	/* The string representing the file name received from the sender */
	string recvFileNameStr = fileName;
	
	/* Append __recv to the end of the file name */
	recvFileNameStr += "__recv";
	
	/* Open the file for writing */
	FILE* fp = fopen(recvFileNameStr.c_str(), "w");
			
	/* Error checks */
	if (!fp)
	{
		perror("fopen");	
		exit(-1);
	}

	/* Keep receiving until the sender sets the size to 0, indicating that
 	 * there is no more data to send.
 	 */	
	while (msgSize != 0)
	{	

		/* Receive the message and get the value of the size field. The message will be of
		 * of type SENDER_DATA_TYPE. That is, a message that is an instance of the message struct
		 * with mtype field set to SENDER_DATA_TYPE (the macro SENDER_DATA_TYPE is defined in
		 * msg.h).  If the size field of the message is not 0, then we copy that many bytes from 
		 * the shared memory segment to the file. Otherwise, if 0, then we close the file 
		 * and exit.
		 *
		 * NOTE: the received file will always be saved into the file called
		 * <ORIGINAL FILENAME__recv>. For example, if the name of the original
		 * file is song.mp3, the name of the received file is going to be song.mp3__recv.
		 */
		message rcvMsg;

		if (msgrcv(msqid, &rcvMsg, sizeof(message) - sizeof(long), SENDER_DATA_TYPE, 0) < 0)
		{
			perror("msgrcv");
			exit(-1);
		}
		
		msgSize = rcvMsg.size;

		/* If the sender is not telling us that we are done, then get to work */
		if (msgSize != 0)
		{
			/* Count the number of bytes received */
			numBytesRecv += msgSize;
			
			/* Save the shared memory to file */
			if (fwrite(sharedMemPtr, sizeof(char), msgSize, fp) < 0)
			{
				perror("fwrite");
				exit(-1);
			}
			
			/* Tell the sender that we are ready for the next set of bytes.
 			 * I.e. send a message of type RECV_DONE_TYPE. That is, a message
			 * of type ackMessage with mtype field set to RECV_DONE_TYPE. 
 			 */
			ackMessage sndMsg;
			sndMsg.mtype = RECV_DONE_TYPE;

			if (msgsnd(msqid, &sndMsg, sizeof(ackMessage) - sizeof(long), 0) < 0)
			{
				perror("msgsnd");
				exit(-1);
			}
		}
		/* We are done */
		else
		{
			/* Close the file */
			fclose(fp);
		}
	}
	
	return numBytesRecv;
}

/**
 * Performs cleanup functions
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
	
	/* Deallocate the shared memory segment */
	if (shmctl(shmid, IPC_RMID, 0) < 0)
	{
		perror("shmctl");
		exit(-1);
	}
	
	/* Deallocate the message queue */
	if (msgctl(msqid, IPC_RMID, 0) < 0)
	{
		perror("msgctl");
		exit(-1);
	}
}

/**
 * Handles the exit signal
 * @param  signal The signal type
 */
void ctrlCSignal(int signal)
{
	/* Free system V resources */
	cleanUp(shmid, msqid, sharedMemPtr);
}

/**
 * Begins program execution
 * @param  argc The number of command line arguments
 * @param  argv An array of C strings containing each command line argument
 */
int main(int argc, char** argv)
{
	
	/* Install a signal handler (see signaldemo.cpp sample file).
 	 * If user presses Ctrl-c, your program should delete the message
 	 * queue and the shared memory segment before exiting. You may add 
	 * the cleaning functionality in ctrlCSignal().
 	 */
	if (signal(SIGINT, ctrlCSignal) == SIG_ERR)
	{
		perror("signal");
		exit(-1);
	}
				
	/* Initialize */
	init(shmid, msqid, sharedMemPtr);
	
	/* Receive the file name from the sender */
	string fileName = recvFileName();
	
	/* Go to the main loop */
	fprintf(stderr, "The number of bytes received is: %lu\n", mainLoop(fileName.c_str()));

	/* Detach from shared memory segment, and deallocate shared memory
	 * and message queue (i.e. call cleanup) 
	 */
	cleanUp(shmid, msqid, sharedMemPtr);
		
	return 0;
}
