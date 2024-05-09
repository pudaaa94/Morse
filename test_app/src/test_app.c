#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* CONSTANTS AND TYPES */
#define MAX_NUM_OF_CHARS 50
#define MAX_NUM_OF_ENCODED_CHARS 1000
#define PATH_TO_DEV_LENGTH 50

typedef enum {
	IDLE,
	NORMAL,
	TEST,
	TEST_ERROR,
	CONFIGURATION,
	QUIT
} work_mode;

const char* work_mode_str[] = {
	"IDLE",
	"NORMAL",
	"TEST",
	"TEST_ERROR",
	"CONFIGURATION",
	"QUIT"
};

const char* test_vector_inputs[] = {
	"A",
	"B",
	"C",
	"AB C",
	"AB CD"
};

const char* test_vector_outputs[] = {
	"* -   ",
	"- * * *   ",
	"- * - *   ",
	"* -   - * * *       - * - *   ",
	"* -   - * * *       - * - *   - * *   "
};

//const char* dev_path = "/dev/morse_dev";
char dev_path[PATH_TO_DEV_LENGTH];

/* FUNCTION PROTOTYPES */
char getch(void);
int sendDataToEncoding(char*, int);
void readEncodedData(void);

/* GLOBAL VARS */

/* sync, time and protection */
static sem_t workModeChanged;
static sem_t semFinishSignal;
static pthread_mutex_t sharedResource;
static pthread_mutex_t sharedResourceTimer;
int enablePeriodicWriting = 0;

/* data holders */
work_mode current_work_mode = IDLE;
unsigned int cmd;
unsigned long arg;
char encodedData[MAX_NUM_OF_ENCODED_CHARS];
char expectedEncodedData[MAX_NUM_OF_ENCODED_CHARS];
char dataToBeEncoded[MAX_NUM_OF_CHARS];
int error_mode = 0;

/* FUNCTION DEFINITIONS */

/* function which triggers write function on driver's side */
int sendDataToEncoding(char* charsToBeEncoded, int len)
{
	int file_desc;
	int ret_val = 0;

	/* Open /dev/morse_dev device. */
	file_desc = open(dev_path, O_RDWR);

	if(file_desc < 0)
	{
		printf("Error opening device handle\n");
		ret_val = file_desc;

		return ret_val;
	}

	/* Write to /dev/morse_dev device */
	ret_val = write(file_desc, charsToBeEncoded, len);
	
	/* Close /dev/morse_dev device */
    	close(file_desc);
    	
    	return ret_val;
}

/* function which triggers read function on driver's side */
void readEncodedData(void)
{
	int file_desc;
	
	/* Init buffer where encoded data from driver will be placed */
    	memset(encodedData, 0, MAX_NUM_OF_ENCODED_CHARS);

	/* Open /dev/morse_dev device. */
	file_desc = open(dev_path, O_RDWR);

	if(file_desc < 0)
	{
		printf("Error opening device handle\n");

		return -1;
	}
	
	/* read from /dev/morse_dev device */
	read(file_desc, encodedData, MAX_NUM_OF_ENCODED_CHARS);
}

/* input thread routine. */
void* inputThreadRoutine (void *param)
{
    char c;
    
    while (1)
    {
        if (sem_trywait(&semFinishSignal) == 0)
        {
            break;
        }
	
	/* current_work_mode used by working thread as well */
	pthread_mutex_lock(&sharedResource);
		printf("\n");
		printf("Currently running in %s mode\n", work_mode_str[current_work_mode]);
		printf("Enter command to change working mode:\n");
		printf("1. q or Q to quit application\n");
		printf("2. n or N to enter NORMAL mode\n");
		printf("3. t or T to enter TEST mode\n");
		printf("4. c or C to enter CONFIGURATION mode\n");
		printf("5. i or I to enter IDLE mode\n");
	pthread_mutex_unlock(&sharedResource);
	
	
	/* accepting input always enabled */
	c = getch();
	//printf("Read char: %c\n", c);

	/* In case of q or Q char, signal both
	threads (including this one) to terminate. */
	if (c == 'q' || c == 'Q')
	{
		/* quiting mode */
		
		/* Changing current work mode, it is read by working thread, protect its access */
		pthread_mutex_lock(&sharedResource);
			current_work_mode = QUIT;
			printf("Changed to %s mode\n", work_mode_str[current_work_mode]);
		pthread_mutex_unlock(&sharedResource);		
		
		/* Terminate threads. Signal the semaphore 3 times in order to notify all threads. */
		sem_post(&semFinishSignal);
		sem_post(&semFinishSignal);
		sem_post(&semFinishSignal);
		
		/* not important in this case */
		sem_post(&workModeChanged);
		
		usleep(1000000); // signaling and reaction to it is not instant, mutex was being locked quicker by this thread than processing thread
	} else{
		if (c == 'c' || c == 'C'){
			/* configure mode */
			
			/* Changing current work mode, cmd and arg, they are read by working thread, protect their access */
			pthread_mutex_lock(&sharedResource);
				current_work_mode = CONFIGURATION;
				printf("Changed to %s mode\n", work_mode_str[current_work_mode]);
				
				printf("\n");
				printf("What you want to configure? (press number of option)\n");
				printf("1. Which LED blinks\n");
				printf("2. Length of one time unit\n");
				printf("3. Driver encodes data with or without errors\n");
				
				c = getch(); /* long waiting for input may cause long delays, because we are holding mutex locked! */
				
				if (c == '1'){
					cmd = 1;
					
					printf("\n");
					printf("1. Left\n");
					printf("2. Right\n");
					
					c = getch(); /* long waiting for input may cause long delays, because we are holding mutex locked! */
					
					if (c == '1') {
						arg = 0;
						
						printf("Configuration done\n");
					} else{
						if (c == '2'){
							arg = 1;
							
							printf("Configuration done\n");
						} else{
							printf("Not supported selection\n");
						}						
					}					
				} else{
					if (c == '2'){
						cmd = 3;
						
						printf("\n");
						printf("Enter amount of seconds for 1 time unit (int number from set [1..9])\n");
						
						c = getch(); /* long waiting for input may cause long delays, because we are holding mutex locked! */
						
						arg = ((int)c - 48) * 1000;					
						
						printf("Configuration done\n");
					} else{
						if (c == '3'){
							cmd = 0;
							
							printf("\n");
							printf("1. With errors\n");
							printf("2. Without errors\n");
							
							c = getch(); /* long waiting for input may cause long delays, because we are holding mutex locked! */
							
							if (c == '1') {
								error_mode = 1;
								arg = 1;
								
								printf("Configuration done\n");
							} else{
								if (c == '2'){
									error_mode = 0;
									arg = 0;
									
									printf("Configuration done\n");
								} else{
									printf("Not supported selection\n");
								}						
							}						
						} else{
							printf("Not supported selection\n");
						}
					}
				}				
				
			pthread_mutex_unlock(&sharedResource);
			
			sem_post(&workModeChanged);
			
			usleep(1000000); // signaling and reaction to it is not instant, mutex was being locked quicker by this thread than processing thread
			
		} else{
			if (c == 't' || c == 'T'){			
				/* Changing current work mode, it is read by working thread, protect its access */
				pthread_mutex_lock(&sharedResource);
					if (error_mode == 1){
						current_work_mode = TEST_ERROR;
					} else{
						current_work_mode = TEST;
					}
					printf("Changed to %s mode\n", work_mode_str[current_work_mode]);
					
					printf("\n");
					printf("Please choose one of test vectors bellow: (type number of desired vector)\n");
					printf("1. %s\n", test_vector_inputs[0]);
					printf("2. %s\n", test_vector_inputs[1]);
					printf("3. %s\n", test_vector_inputs[2]);
					printf("4. %s\n", test_vector_inputs[3]);
					printf("5. %s\n", test_vector_inputs[4]);
					
					c = getch(); /* long waiting for input may cause long delays, because we are holding mutex locked! */
					
					if (c >= 49 && c <= 57){						
						//usleep(2000); /* driver will start writing its own log messages */
						
						/* prepare data for processing thread */
						memset(dataToBeEncoded, 0, MAX_NUM_OF_CHARS);
						memcpy(dataToBeEncoded, test_vector_inputs[(int)c-49], strlen(test_vector_inputs[(int)c-49]));						
						memset(expectedEncodedData, 0, MAX_NUM_OF_ENCODED_CHARS);
						memcpy(expectedEncodedData, test_vector_outputs[(int)c-49], strlen(test_vector_outputs[(int)c-49]));
					} else{
						printf("Not supported selection\n");
					}												
				pthread_mutex_unlock(&sharedResource);	
				
				sem_post(&workModeChanged);
				
				usleep(1000000);  // signaling and reaction to it is not instant, mutex was being locked quicker by this thread than processing thread		
			} else{
				if (c == 'n' || c == 'N'){
					if (error_mode == 1){
						pthread_mutex_lock(&sharedResource);
							printf("Driver remained in ERROR mode, please configure it back to NORMAL mode and then initiate this mode\n");
						pthread_mutex_unlock(&sharedResource);
					} else{	
						if (current_work_mode == NORMAL){ /* protection not needed here, processing thread only reads this and we are reading here as well */
							pthread_mutex_lock(&sharedResource);
								printf("Already in NORMAL mode\n");
							pthread_mutex_unlock(&sharedResource);
						} else{
							pthread_mutex_lock(&sharedResource);
								current_work_mode = NORMAL;
								printf("Changed to %s mode\n", work_mode_str[current_work_mode]);						
							pthread_mutex_unlock(&sharedResource);	
					
							sem_post(&workModeChanged);
							
							usleep(1000000); // signaling and reaction to it is not instant, mutex was being locked quicker by this thread than processing thread
						}	
					}					
				} else{
					if(c == 'i' || c == 'I'){
						pthread_mutex_lock(&sharedResource);
							current_work_mode = IDLE;
							printf("Changed to %s mode\n", work_mode_str[current_work_mode]);							
						pthread_mutex_unlock(&sharedResource);
						
						sem_post(&workModeChanged);
						usleep(1000000);  // signaling and reaction to it is not instant, mutex was being locked quicker by this thread than processing thread
					} else{
						pthread_mutex_lock(&sharedResource);
							printf("Not supported selection\n");
						pthread_mutex_unlock(&sharedResource);
					}
				}
			}
		}
	}
    }

    return 0;
}

/* processing thread routine. */
void* processingThreadRoutine (void *param)
{
    work_mode current_work_mode_local;
    int file;
    
    while (1)
    {
        if (sem_trywait(&semFinishSignal) == 0)
        {
            	/* we have printings in input thread */
		pthread_mutex_lock(&sharedResource);
			printf("Quiting...\n");
		pthread_mutex_unlock(&sharedResource);
        
		break;
        }
        
        if (sem_trywait(&workModeChanged) == 0){   
         	
        	/* Reading current work mode, it is changed by input thread, protect its access */
		pthread_mutex_lock(&sharedResource);
			current_work_mode_local = current_work_mode;
		pthread_mutex_unlock(&sharedResource);
		
		switch (current_work_mode_local){
		
			case IDLE:
			
				/* enablePeriodicWriting being read in timer thread */
				pthread_mutex_lock(&sharedResourceTimer);
					/* disable automatic sending of data and wait for work mode selection */
					enablePeriodicWriting = 0;
					//printf("Disabled periodic: %d\n", enablePeriodicWriting);
				pthread_mutex_unlock(&sharedResourceTimer);
				
				break;
				
			case NORMAL:
			
				/* enablePeriodicWriting being read in timer thread */
				pthread_mutex_lock(&sharedResourceTimer);
					enablePeriodicWriting = 1;
				pthread_mutex_unlock(&sharedResourceTimer);
				
				break;
			
			case TEST:
			case TEST_ERROR:
				
				/* enablePeriodicWriting being read in timer thread */
				pthread_mutex_lock(&sharedResourceTimer);
					enablePeriodicWriting = 0;
					//printf("Disabled periodic: %d\n", enablePeriodicWriting);
				pthread_mutex_unlock(&sharedResourceTimer);
				
				/* we have printings in input thread + dataToBeEncoded changes in input thread */
				pthread_mutex_lock(&sharedResource);						
					
					/* trigger write function on driver's side */
					if (sendDataToEncoding(dataToBeEncoded, strlen(dataToBeEncoded)) <= 0){
						printf("Writing to device failed (either device file handle couldn't be opened or LED is still blinking)\n");
					} else{
						printf("Encoding: %s\n", dataToBeEncoded);
						printf("Expected output is: %s\n", expectedEncodedData);
						
						/* writing is successful */
						usleep(2000); /* encoding is really quick, but just in case */
					
						/* trigger read function on driver's side */
						readEncodedData();
						
						printf("Encoded data: %s\n", encodedData);
						
						if (strlen(encodedData) != strlen(expectedEncodedData)){
							printf("Test failed\n");
						} else{
							if (0 != memcmp(encodedData, expectedEncodedData, strlen(encodedData))){
								printf("Test failed\n");
							} else{
								printf("Test passed\n");
							}
						}
					}						
									
				pthread_mutex_unlock(&sharedResource);
			
				break;
			
			case CONFIGURATION:
			
				/* working with cmd and arg, which are being changed in input thread */
				pthread_mutex_lock(&sharedResource);				
					
					/* perform io call to driver to notify it */
					if ((file = open(dev_path, O_RDWR)) < 0) {
						printf("Error opening device handle\n");
						pthread_mutex_unlock(&sharedResource);
						
						return -1;
				  	}
					
					//printf("CMD: %d, ARG: %d\n", cmd, arg);
					if (ioctl(file, cmd, arg)) {
						printf("Error during ioctl call: %s\n", strerror(errno));
						//printf("Cmd: %d\n", cmd);
						//printf("Arg: %d\n", arg);
						pthread_mutex_unlock(&sharedResource);
						
						return -1;
				  	}		  	
				  				  	
					close(file);
					
				pthread_mutex_unlock(&sharedResource);
			
				break;
			
			case QUIT:
				/* do nothing, logic is contained where break of while loop is */
		}
        }
    }

    return 0;
}

/* timer thread routine. */
void* timerThreadRoutine (void *param)
{   
	int enablePeriodicWritingLocal = 0;
	int i = 0;

	while (1)
	{
		if (sem_trywait(&semFinishSignal) == 0)
		{        
			break;
		}

		/* enablePeriodicWriting being changed in processing thread */
		pthread_mutex_lock(&sharedResourceTimer);
			//printf("Reading done: %d\n", enablePeriodicWriting);
			enablePeriodicWritingLocal = enablePeriodicWriting;
		pthread_mutex_unlock(&sharedResourceTimer);
		
		if (enablePeriodicWritingLocal == 1){	
			/* generate new data and try to send */
			
			/* printing and access to dataToBeEncoded */
			pthread_mutex_lock(&sharedResource);
				memset(dataToBeEncoded, 0, MAX_NUM_OF_CHARS);
				
				for (i = 0; i < 4; i++){
					dataToBeEncoded[i] = (char)(rand() % 26) + 65;
				}
				
				printf("Data to be encoded: %s\n", dataToBeEncoded);
				
				/* trigger write function on driver's side */
				if (sendDataToEncoding(dataToBeEncoded, strlen(dataToBeEncoded)) <= 0){
					printf("Writing to device failed (either device file handle couldn't be opened or LED is still blinking)\n");
				} else{
					printf("Encoding: %s\n", dataToBeEncoded);
					
					/* writing is successful */
					usleep(2000); /* encoding is really quick, but just in case */
				
					/* trigger read function on driver's side */
					readEncodedData();
					
					printf("Encoded data: %s\n", encodedData);
				}
				
			pthread_mutex_unlock(&sharedResource);
				
			/* sleep for 2 seconds */
			usleep(2000000);
		}
	}

	return 0;
}

/* Main thread creates two additinoal threads (the producer and the consumer) and waits them to terminate. */
int main (int argc, char *argv[])
{
	if (argc != 2) {
		printf("Wrong number of arguments\n");
		return -1;
	}
	
	memset(dev_path, 0, PATH_TO_DEV_LENGTH);
	memcpy(dev_path, argv[1], strlen(argv[1]));
	
	/* Thread IDs. */
	pthread_t inputHandlingThread;
	pthread_t processingHandlingThread;   
	pthread_t timerThread;

	/* Create semaphores. */
	sem_init(&workModeChanged, 0, 0);
	sem_init(&semFinishSignal, 0, 0);

	/* Initialise mutex. */
	pthread_mutex_init(&sharedResource, NULL);
	pthread_mutex_init(&sharedResourceTimer, NULL);

	/* Create threads: the producer and the consumer. */
	pthread_create(&inputHandlingThread, NULL, inputThreadRoutine, 0);
	pthread_create(&processingHandlingThread, NULL, processingThreadRoutine, 0);
	pthread_create(&timerThread, NULL, timerThreadRoutine, 0);

	/* Join threads (wait them to terminate) */
	pthread_join(inputHandlingThread, NULL);
	pthread_join(processingHandlingThread, NULL);
	pthread_join(timerThread, NULL);

	/* Release resources. */
	fflush(stdout);
	sem_destroy(&workModeChanged);
	sem_destroy(&semFinishSignal);
	pthread_mutex_destroy(&sharedResource);
	pthread_mutex_destroy(&sharedResourceTimer);

	printf("\n");

	return 0;
}
