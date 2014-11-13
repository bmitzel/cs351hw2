#include <sys/shm.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* The maximum size of the file name */
#define MAX_FILE_NAME_SIZE 100

/* The size of the shared memory chunk */
#define SHARED_MEMORY_CHUNK_SIZE 1000

/* The id for the shared memory segment */
int shmid;

/* The pointer to the shared memory */
void* sharedMemPtr;

/* The receiver's pid */
pid_t rpid;

/* The masks of signals to temporarily block */
sigset_t mask;
sigset_t oldmask;

/* The user interrupt flag */
bool usr_interrupt;

/**
 * Sets up the shared memory segment
 * @param  shmid The id of the allocated shared memory
 */
void init(int& shmid, void*& sharedMemPtr)
{
	/* Generate the key for the shared memory segment */
	key_t key = ftok("keyfile.txt", 'a');

	/* Failed to generate the key */
	if (key < 0)
	{
		perror("ftok");
		exit(-1);
	}

	/* Get the shared memory segment ID */
	shmid = shmget(key, SHARED_MEMORY_CHUNK_SIZE + sizeof(size_t), S_IRUSR | S_IWUSR);

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

	/* Initialize the mask of blocking signals to empty */
	if (sigemptyset(&mask) < 0)
	{
		perror("sigemptyset");
		exit(-1);
	}

	/* Add SIGUSR2 to the mask of blocking signals */
	if (sigaddset(&mask, SIGUSR2) < 0)
	{
		perror("sigaddset");
		exit(-1);
	}

	/* Initialize the user interrupt flag to false */
	usr_interrupt = false;
}

/**
 * Performs the cleanup functions
 * @param  sharedMemPtr The pointer to the shared memory
 * @param  shmid The id of the shared memory segment
 */
void cleanUp(const int& shmid, void* sharedMemPtr)
{
	/* Detach from shared memory */
	if (shmdt(sharedMemPtr) < 0)
	{
		perror("shmdt");
		exit(-1);
	}
}

/**
 * Sleeps until a specific signal is received
 * @param  flag The flag that is set once the signal is received
 */
void wait(bool& flag)
{
	if (sigprocmask(SIG_BLOCK, &mask, &oldmask) < 0)
	{
		perror("sigprocmask");
		exit(-1);
	}

	while (!flag)
	{
		sigsuspend(&oldmask);
	}

	if (sigprocmask(SIG_UNBLOCK, &mask, NULL) < 0)
	{
		perror("sigprocmask");
		exit(-1);
	}

	flag = false;
}

/**
 * Stores the size of the chunk saved to shared memory
 * and advances the pointer to the beginning of the chunk
 * @param  size The size of the next chunk in bytes
 */
void setChunkSize(size_t size)
{
	sharedMemPtr = static_cast<char*>(sharedMemPtr) - sizeof(size_t);
	*static_cast<size_t*>(sharedMemPtr) = size;
	sharedMemPtr = static_cast<char*>(sharedMemPtr) + sizeof(size_t);
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

	/* The number of bytes saved to shared memory */
	size_t chunkSize;

	/* The number of bytes sent */
	unsigned long numBytesSent = 0;
	
	/* Was the file open? */
	if (!fp)
	{
		perror("fopen");
		exit(-1);
	}
	
	/* Advance the shared memory pointer by sizeof(size_t) for writing data
	 * since the first bytes in shared memory will store chunkSize
	 */
	sharedMemPtr = static_cast<char*>(sharedMemPtr) + sizeof(size_t);

	/* Read the whole file */
	while (!feof(fp))
	{
		/* Read at most SHARED_MEMORY_CHUNK_SIZE from the file and store them in shared memory.
 		 * fread will return how many bytes it has actually read (since the last chunk may be less
 		 * than SHARED_MEMORY_CHUNK_SIZE).
 		 */
		if ((chunkSize = fread(sharedMemPtr, sizeof(char), SHARED_MEMORY_CHUNK_SIZE, fp)) < 0)
		{
			perror("fread");
			exit(-1);
		}
		
		/* Store the chunk size in shared memory and advance the shared memory pointer */
		setChunkSize(chunkSize);

		/* Count the number of bytes sent */
		numBytesSent += chunkSize;

		/* Signal the receiver that the data is ready */
		if (kill(rpid, SIGUSR1) < 0)
		{
			perror("kill");
			exit(-1);
		}

		/* Wait for signal from receiver */
		wait(usr_interrupt);
	}
	
	/* Set the size of the chunk to zero to signal that there is no more data to send */
	chunkSize = 0;

	/* Store the chunk size in shared memory and advance the shared memory pointer */
	setChunkSize(chunkSize);

	/* Signal the receiver that the data is ready */
	if (kill(rpid, SIGUSR1) < 0)
	{
		perror("kill");
		exit(-1);
	}

	/* Close the file */
	fclose(fp);
	
	/* Back up the shared memory pointer */
	sharedMemPtr = static_cast<char*>(sharedMemPtr) - sizeof(size_t);

	return numBytesSent;
}

/**
 * Handles the SIGUSR2 signal
 */
void usr2Signal(int signal)
{
	/* Set user interrupt flag to true */
	usr_interrupt = true;
}

/**
 * Sends the pid of this process through shared memory
 */
void sendpid()
{
	*static_cast<pid_t*>(sharedMemPtr) = getpid();

	/* Signal the receiver to get the pid */
	if (kill(rpid, SIGUSR1) < 0)
	{
		perror("kill");
		exit(-1);
	}
}

/**
 * Receives the pid of the receiver through shared memory
 * @return The receiver's pid
 */
pid_t recvpid()
{
	return *static_cast<pid_t*>(sharedMemPtr);
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

	/* Wait for signal from receiver */
	wait(usr_interrupt);

	/* Store the file name in shared memory */
	strcpy(static_cast<char*>(sharedMemPtr), fileName);

	/* Signal the receiver to get the file name */
	if (kill(rpid, SIGUSR1) < 0)
	{
		perror("kill");
		exit(-1);
	}

	/* Wait for signal from receiver */
	wait(usr_interrupt);
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
		
	/* Install a signal handler for the SIGUSR2 signal */
	if (signal(SIGUSR2, usr2Signal) == SIG_ERR)
	{
		perror("signal");
		exit(-1);
	}

	/* Initialize */
	init(shmid, sharedMemPtr);
	
	/* Get the pid of the receiver */
	rpid = recvpid();

	/* Send the pid of this process */
	sendpid();

	/* Send the name of the file */
	sendFileName(argv[1]);

	/* Send the file */
	fprintf(stderr, "The number of bytes sent is %lu\n", sendFile(argv[1]));
	
	/* Cleanup */
	cleanUp(shmid, sharedMemPtr);
		
	return 0;
}
