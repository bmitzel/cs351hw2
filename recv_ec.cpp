#include <sys/shm.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>

using namespace std;

/* The size of the shared memory chunk */
#define SHARED_MEMORY_CHUNK_SIZE 1000

/* The id for the shared memory segment */
int shmid;

/* The pointer to the shared memory */
void* sharedMemPtr;

/* The sender's pid */
pid_t spid;

/* The masks of signals to temporarily block */
sigset_t mask;
sigset_t oldmask;

/* The user interrupt flag */
bool usr_interrupt;

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
 * Gets the size of the next chunk to read from shared memory
 * and advances the pointer to the beginning of the chunk
 * @return The size of the next chunk in bytes
 */
size_t getChunkSize()
{
	sharedMemPtr = static_cast<char*>(sharedMemPtr) - sizeof(size_t);
	size_t chunkSize = *static_cast<size_t*>(sharedMemPtr);
	sharedMemPtr = static_cast<char*>(sharedMemPtr) + sizeof(size_t);
	return chunkSize;
}

/**
 * The function for receiving the name of the file
 * @return The name of the file received from the sender
 */
string recvFileName()
{
	/* The file name read from shared memory */
	string fileName;

	/* Signal the sender to send the file name */
	if (kill(spid, SIGUSR2) < 0)
	{
		perror("kill");
		exit(-1);
	}

	/* Wait for signal from sender */
	wait(usr_interrupt);

	/* Get the file name */
	fileName = static_cast<char*>(sharedMemPtr);

	/* Signal acknowledgment to sender */
	if (kill(spid, SIGUSR2) < 0)
	{
		perror("kill");
		exit(-1);
	}

	return fileName;
}

 /**
  * Sets up the shared memory segment
  * @param  shmid The id of the allocated shared memory
  * @param  sharedMemPtr The pointer to the shared memory
  */
void init(int& shmid, void*& sharedMemPtr)
{
	/* Generate a key for the shared memory segment */
	key_t key = ftok("keyfile.txt", 'a');

	/* Failed to generate the key */
	if (key < 0)
	{
		perror("ftok");
		exit(-1);
	}

	/* Allocate a shared memory segment with sizeof(size_t) additional space
	 * The additional space is used for storing the size of each chunk being transferred
	 */
	shmid = shmget(key, SHARED_MEMORY_CHUNK_SIZE + sizeof(size_t), IPC_CREAT | S_IRUSR | S_IWUSR);

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

	/* Initialize the mask of blocking signals to empty */
	if (sigemptyset(&mask) < 0)
	{
		perror("sigemptyset");
		exit(-1);
	}

	/* Add SIGUSR1 to the mask of blocking signals */
	if (sigaddset(&mask, SIGUSR1) < 0)
	{
		perror("sigaddset");
		exit(-1);
	}

	/* Initialize the user interrupt flag to false */
	usr_interrupt = false;
}

/**
 * The main loop
 * @param  fileName The name of the file received from the sender
 * @return The number of bytes received
 */
unsigned long mainLoop(const char* fileName)
{
	/* The size of the last chunk received from the sender */
	size_t chunkSize = -1;
	
	/* The total number of bytes received */
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

	/* Advance the shared memory pointer by sizeof(size_t) for reading data
	 * since the first bytes in shared memory will store chunkSize
	 */
	sharedMemPtr = static_cast<char*>(sharedMemPtr) + sizeof(size_t);

	/* Keep receiving until the sender sets the size to 0, indicating that
 	 * there is no more data to send.
 	 */	
	while (chunkSize != 0)
	{	
		/* Wait for signal from sender */
		wait(usr_interrupt);

		/* Get the size of the next chunk of data being sent */
		chunkSize = getChunkSize();

		/* If the sender is not telling us that we are done, then get to work */
		if (chunkSize != 0)
		{
			/* Count the number of bytes received */
			numBytesRecv += chunkSize;
			
			/* Save the shared memory to file */
			if (fwrite(sharedMemPtr, sizeof(char), chunkSize, fp) < 0)
			{
				perror("fwrite");
				exit(-1);
			}

			/* Signal the sender to send the next chunk */
			if (kill(spid, SIGUSR2) < 0)
			{
				perror("kill");
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
	
	/* Back up the shared memory pointer */
	sharedMemPtr = static_cast<char*>(sharedMemPtr) - sizeof(size_t);

	return numBytesRecv;
}

/**
 * Performs cleanup functions
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
	
	/* Deallocate the shared memory segment */
	if (shmctl(shmid, IPC_RMID, 0) < 0)
	{
		perror("shmctl");
		exit(-1);
	}
}

/**
 * Handles the SIGUSR1 signal
 */
void usr1Signal(int signal)
{
	/* Set user interrupt flag to true */
	usr_interrupt = true;
}

/**
 * Handles the exit signal
 * @param  signal The signal type
 */
void ctrlCSignal(int signal)
{
	/* Free system V resources */
	cleanUp(shmid, sharedMemPtr);
	exit(-1);
}

/**
 * Sends the pid of this process through shared memory
 */
void sendpid()
{
	*static_cast<pid_t*>(sharedMemPtr) = getpid();
}

/**
 * Receives the pid of the sender through shared memory
 * @return The sender's pid
 */
pid_t recvpid()
{
	/* Wait for signal from sender */
	wait(usr_interrupt);

	return *static_cast<pid_t*>(sharedMemPtr);
}

/**
 * Begins program execution
 * @param  argc The number of command line arguments
 * @param  argv An array of C strings containing each command line argument
 * @return The exit code
 */
int main(int argc, char** argv)
{
	
	/* Install a signal handler (see signaldemo.cpp sample file).
 	 * If user presses Ctrl-c, your program should delete the
 	 * shared memory segment before exiting. You may add
	 * the cleaning functionality in ctrlCSignal().
 	 */
	if (signal(SIGINT, ctrlCSignal) == SIG_ERR)
	{
		perror("signal");
		exit(-1);
	}

	/* Install a signal handler for the SIGUSR1 signal */
	if (signal(SIGUSR1, usr1Signal) == SIG_ERR)
	{
		perror("signal");
		exit(-1);
	}
				
	/* Initialize */
	init(shmid, sharedMemPtr);
	
	/* Send the pid of this process */
	sendpid();

	/* Get the pid of the sender */
	spid = recvpid();

	/* Receive the file name from the sender */
	string fileName = recvFileName();

	/* Go to the main loop */
	fprintf(stderr, "The number of bytes received is: %lu\n", mainLoop(fileName.c_str()));

	/* Detach from shared memory segment and deallocate shared memory
	 * (i.e. call cleanup)
	 */
	cleanUp(shmid, sharedMemPtr);
		
	return 0;
}
