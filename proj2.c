#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <string.h>

#define SEM_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)
#define SEM_NAME "/gorkems_semaphore"
#define FILE_SEM_NAME "/file_semaphore"
#define FILE_NAME "proj2.out"
#define OUTPUT_STRING_SIZE 70
#define ACTION_NO_STRING_SIZE 10
#define NAME_STRING_SIZE 10
#define NAME_ACTION_STRING_SIZE 40
#define STATISTICS_STRING_SIZE 20

#define IMM_NONEXISTENT 0
#define IMM_WANTS_TO_ENTER 1
#define IMM_ENTERS 2
#define IMM_CHECKS 3
#define IMM_APPROVED 4
#define IMM_WANTS 5
#define IMM_GETS 6
#define IMM_WANTS_TO_LEAVE 7
#define IMM_LEAVES 8
#define J_NONEXISTENT 0
#define J_WANTS_TO_ENTER 1
#define J_ENTERS 2
#define J_STARTS 3
#define J_ENDS 4
#define J_LEAVES 5
#define J_FINISHES 6

void customSleep(int);
void createImmigrants(int);
void immigrantFunc(int);
void judgeFunc();
bool initSemaphoreAndSharedMemory();
void outputToFile(char*, char*, int);
void cleanUp();

typedef struct commonMemory {
	int immigrants[100];
	int judgeStatus;
	int actionCounter;
	int totalImmigrants;
	int inBuildingNotDecided;
	int registeredNotDecided;
	int inBuilding;
	int approved;
	int leftBuilding;
} commonMemory;

commonMemory *memory;
FILE * fp;

int NO_OF_IMM;
int JUDGE_WAIT_TIME;
int IMMIGRANT_CREATION_TIME;
int CERTIFICATE_RETRIEVAL_TIME;
int JUDGE_DECISION_TIME;

int main(int argc, char *argv[]) {
	pid_t judge, immigrantCreator;
	int i;
	
	// Check if the number of arguments passed from the command line match the required amount of data for the program to run
	if (argc != 6){
		printf("ERROR: Inappropriate number of console arguments.\n");
		return -1;
	}
  	NO_OF_IMM = atoi(argv[1]);
	IMMIGRANT_CREATION_TIME = atoi(argv[2]);
	JUDGE_WAIT_TIME = atoi(argv[3]);
	CERTIFICATE_RETRIEVAL_TIME = atoi(argv[4]);
	JUDGE_DECISION_TIME = atoi(argv[5]);

	// Initialize necessary structures, file pointers, semaphores and shared memory segments
	sem_t *semaphore = sem_open(SEM_NAME, O_CREAT | O_EXCL, SEM_PERMS, 1);
	sem_t *fileSemaphore = sem_open(FILE_SEM_NAME, O_CREAT | O_EXCL, SEM_PERMS, 1);
	memory = mmap(NULL, sizeof(commonMemory), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	fp = fopen (FILE_NAME,"w");
	// Abort and clean everything up if something went wrong
	if (semaphore == SEM_FAILED || fileSemaphore == SEM_FAILED || memory == MAP_FAILED || fp == NULL){
	printf("ERROR creating semaphore, output file or shared memory.\n");
	cleanUp();
		return -1;
	}
	// Assign initial values to the variables in shared memory segment
	for (i=0;i<NO_OF_IMM;i++){
		memory->immigrants[i] = IMM_NONEXISTENT;
	}
	memory->judgeStatus = J_NONEXISTENT;
	memory->actionCounter = 0;
	memory->totalImmigrants = 0;
	memory->inBuildingNotDecided = 0;
	memory->registeredNotDecided = 0;
	memory->inBuilding = 0;
	memory->leftBuilding = 0;
	memory->approved = 0;

	// For a better output design on the console
	printf("\n");

	// Creating a new process that'll be creating new processes for new immigrants. 
	immigrantCreator = fork();
	if (immigrantCreator < 0){
		printf("ERROR forking immigrantCreator.\n");
		cleanUp();
		exit(EXIT_FAILURE);
	}
	
	// Creating a new process that'll act as the judge.
	if (immigrantCreator > 0){
		judge = fork();
		if (judge < 0){
			printf("ERROR forking immigrant.\n");
			cleanUp();
			exit(EXIT_FAILURE);
		}
	}
	
	// Calling the function that creates new immigrants. Function will be called only by the immigrantCreator process.
	if (immigrantCreator == 0){
		createImmigrants(NO_OF_IMM);
	}
	
	// Calling the lifespan function of the judge, only by the judge process.
	if (judge == 0){
		judgeFunc();
	}
	
	// Closing the semaphore for the main process.
	if (sem_close(semaphore) < 0) {
		perror("ERROR sem_close failed\n");
		cleanUp();
		exit(EXIT_FAILURE);
	}

	// Wait until the judge is finished and all immigrants have left the building
	while(memory->judgeStatus != J_FINISHES || memory-> inBuilding != 0){}
	
	// Clean up and close all the semaphores, file pointers, shared memory segments.
	cleanUp();

	return 0;
}

// A function for releasing the memory that was allocated for the shared memory, unlinking semaphores and closing file pointers.
void cleanUp(){
	munmap(memory, sizeof(commonMemory));
	sem_unlink(SEM_NAME);
	sem_unlink(FILE_SEM_NAME);
	fclose(fp);
}

// Function for making processes sleep for a duration between 0 and the given upper limit
void customSleep(int maxDuration){
 maxDuration = maxDuration * 1000000;
 if (maxDuration != 0)
  usleep(rand() % maxDuration);
}

// A function for creating multiple immigrant processes.
void createImmigrants(int amount){
	int i;
	for (i=0;i<amount;i++){
		if (fork() == 0){
			immigrantFunc(i);
			memory->totalImmigrants++;
		} else
			customSleep(IMMIGRANT_CREATION_TIME);
	}
	
	exit(0);
}

// The function that acts as the whole lifespan of an immigrant process
void immigrantFunc(int no){
	// Access the semaphore that was previously created by the main function
	sem_t *semaphore = sem_open(SEM_NAME, O_RDWR);
	// Name the immigrant based on its number
	char name[NAME_STRING_SIZE];
	sprintf(name, "IMM %d", no+1);
	// Abort if access to semaphore was not successful
	if (semaphore == SEM_FAILED) {
		perror("ERROR sem_open failed");
		exit(EXIT_FAILURE);
	}
	outputToFile(name, "starts", 0);
	memory->immigrants[no] = IMM_WANTS_TO_ENTER;
	// Wait until judge leaves the hall
	while(memory->judgeStatus > J_WANTS_TO_ENTER && memory->judgeStatus < J_LEAVES){}
	// Wait for the semaphore to be available
	if (sem_wait(semaphore) < 0) 
		printf("ERROR accessing semaphore.\n");
	// Enter the hall and check in
	memory->immigrants[no]=IMM_ENTERS;
	memory->inBuilding++;
	memory->inBuildingNotDecided++;
	outputToFile(name, "enters", 1);
	memory->immigrants[no]=IMM_CHECKS;
	memory->registeredNotDecided++;
	outputToFile(name, "checks", 1);
	// Release semaphore for other processes
	if (sem_post(semaphore) < 0) {
		printf("ERROR sem_post failed\n");
	}
	// Wait until the application was approved and the judge is done with the approvals
	while (memory->immigrants[no] != IMM_APPROVED || memory->judgeStatus == J_STARTS){}
	// Ask for the certificate, wait for the semaphore to be released, get the certificate and release the semaphore
	memory->immigrants[no]=IMM_WANTS;
	outputToFile(name, "wants certificate", 1);
	if (sem_wait(semaphore) < 0) 
		printf("ERROR accessing semaphore.\n");
	customSleep(CERTIFICATE_RETRIEVAL_TIME);
	memory->immigrants[no] = IMM_GETS;
	outputToFile(name, "got certificate", 1);
	if (sem_post(semaphore) < 0) 
		printf("ERROR sem_post failed\n");
	memory->immigrants[no] = IMM_WANTS_TO_LEAVE;
	// Wait until judge leaves the hall to leave
	while (memory->judgeStatus > J_WANTS_TO_ENTER && memory->judgeStatus < J_LEAVES){}
	if (sem_wait(semaphore) < 0) 
		printf("ERROR accessing semaphore.\n");
	memory->immigrants[no] = IMM_LEAVES;
	memory->leftBuilding++;
	memory->inBuilding--;
	outputToFile(name, "leaves", 1);
	if (sem_post(semaphore) < 0) 
		printf("ERROR sem_post failed\n");
	// Close the semaphore and end the process for this immigrant
	if (sem_close(semaphore) < 0)
			perror("sem_close(3) failed");
	exit(0);
}

// The function that acts as the whole lifespan of the judge
void judgeFunc (){
	int i;
	char* name = "JUDGE";
	// Access the semaphore that was previously created by the main function
	sem_t *semaphore = sem_open(SEM_NAME, O_RDWR);
	if (semaphore == SEM_FAILED) {
		perror("ERROR sem_open failed");
		exit(EXIT_FAILURE);
	}
	memory->judgeStatus = J_STARTS;
	// Judge process will be running this while loop until all immigrants have received their certificates
	while (memory->approved < NO_OF_IMM){
		// Waits for a while and then wants to enter the hall
		customSleep(JUDGE_WAIT_TIME);
		memory->judgeStatus = J_WANTS_TO_ENTER;
		outputToFile(name, "wants to enter", 0);
		// Since the hall has only one entrance, judge waits for the semaphore to be released
		if (sem_wait(semaphore) < 0) 
			printf("ERROR accessing semaphore.\n");
		memory->judgeStatus=J_ENTERS;
		outputToFile(name, "enters", 1);
		// If there are immigrants in the building that haven't checked in yet, judge waits for them
		if (memory->inBuildingNotDecided != memory->registeredNotDecided){
			outputToFile(name, "waits for imm", 1);
			while (memory->inBuildingNotDecided != memory->registeredNotDecided){}
		}
		// Waits for a while and then starts approving applications
		customSleep(JUDGE_DECISION_TIME);
		memory->judgeStatus = J_STARTS;
		outputToFile(name, "starts confirmation", 1);
		// Judge goes through all the immigrants and applies those who checked in
		for (i=0;i<NO_OF_IMM;i++){
			if (memory->immigrants[i] == IMM_CHECKS){
				memory->immigrants[i] = IMM_APPROVED;
				memory->inBuildingNotDecided--;
				memory->registeredNotDecided--;
				memory->approved++;
			}
		}
		// Ends approval and releases semaphore
		memory->judgeStatus = J_ENDS;
		outputToFile(name, "ends confirmation", 1);
		if (sem_post(semaphore) < 0) 
			printf("ERROR sem_post failed\n");
		// Leaves the hall after a period of waiting
		customSleep(JUDGE_WAIT_TIME);
		memory->judgeStatus = J_LEAVES;
		outputToFile(name, "leaves", 1);
	}
	// Once all immigrants have been approved, judge closes the semaphore and the process ends
	memory->judgeStatus = J_FINISHES;
	outputToFile(name, "finishes", 0);
	if (sem_close(semaphore) < 0)
			perror("sem_close(3) failed");
	exit(0);
}

// A function to write outputs to the console and the output file
void outputToFile(char* name, char* action, int printStatistics){
	sem_t *fileSemaphore = sem_open(FILE_SEM_NAME, O_RDWR);
	if (fileSemaphore == SEM_FAILED) {
		perror("ERROR sem_open failed");
		exit(EXIT_FAILURE);
	}
	if (sem_wait(fileSemaphore) < 0) 
		printf("ERROR accessing semaphore.\n");
	char string[OUTPUT_STRING_SIZE];
	char actionNo[ACTION_NO_STRING_SIZE];
	char nameAndAction[NAME_ACTION_STRING_SIZE];
	char statistics[STATISTICS_STRING_SIZE];
	memory->actionCounter++;
	sprintf(actionNo, "%d : ", memory->actionCounter);
	strcpy(string, actionNo);
	sprintf(nameAndAction, "%s : %s", name, action);
	strcat(string, nameAndAction);
	if (printStatistics == 1){
		sprintf(statistics, " : %d : %d : %d\n", memory->inBuildingNotDecided, memory->registeredNotDecided, memory->inBuilding);
		strcat(string, statistics);
	} else {
		strcat(string, "\n");
	}
	fprintf(fp, "%s", string);
	fflush(fp);
	if (sem_post(fileSemaphore) < 0) 
		printf("ERROR sem_post failed\n");
}
