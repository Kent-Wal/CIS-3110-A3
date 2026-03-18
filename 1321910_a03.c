#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <semaphore.h>
#include <stdbool.h>

void logStart(char* tID);//function to log that a new thread is started
void logFinish(char* tID);//function to log that a thread has finished its time

void startClock();//function to start program clock
long getCurrentTime();//function to check current time since clock was started
time_t programClock;//the global timer/clock for the program

typedef struct thread //represents a single thread, you can add more members if required
{
	char tid[4];//id of the thread as read from file, set in readFile() for you
	unsigned int startTime;//start time of thread as read from file, set in readFile() for you
	int state;//you can use it as per your desire
	pthread_t handle;//you can use it as per your desire
	int retVal;//you can use it as per your desire
} Thread;

void* threadRun(void* t);//the thread function, the code executed by each thread
int readFile(char* fileName, Thread** threads);//function to read the file content and build array of threads
void sortThreadsByStartTime(Thread* threads, int count);//function to sort threads by their start time


//variable declarations
sem_t even_sem;        //even threads wait here
sem_t odd_sem;         //odd threads wait here
sem_t mutex;           //only 1 thread in critical section at a time

int last_parity = -1;  // -1 = no thread has gone yet, 0 = even went last, 1 = odd went last
int even_remaining = 0;
int odd_remaining = 0;
int even_waiting = 0;
int odd_waiting = 0;
bool all_started = false;   //true if all threads have started, false otherwise

pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER; // protects the globals above
pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;   // signals when a thread has logged start
pthread_cond_t finish_cond = PTHREAD_COND_INITIALIZER;  // signals when a thread has left critical section

int main(int argc, char *argv[]){
	Thread* threads = NULL;
	int threadCount = -1;

	//check if the user provided the file name as command line argument
	//and read input file
	if(argc != 2){
		printf("Usage: %s <input_file>\n", argv[0]);
		return -1;
	}
	threadCount = readFile(argv[1], &threads);
	if(threadCount < 0){
		return -1;
	}

	//sort threads by their start time
	sortThreadsByStartTime(threads, threadCount);

	//start program clock
	startClock();

	//count odd/even threads before starting
	for (int i = 0; i < threadCount; i++) {
		int y = threads[i].tid[2] - '0';  //get y integer value
		if (y % 2 == 0) even_remaining++;
		else odd_remaining++;
	}

	// initialize semaphores
	sem_init(&even_sem, 0, 0);
	sem_init(&odd_sem, 0, 0);
	sem_init(&mutex, 0, 1);		//can only have 1 at a time in critical section

	//create threads at their start times
	for(int i = 0; i < threadCount; i++){
		Thread* thread = &threads[i];
		while(getCurrentTime() < thread->startTime){
			usleep(10000); // sleep for 10ms to avoid busy waiting
		}

		if (i == threadCount - 1) {
			int wake_even = 0;
			int wake_odd = 0;
			int target_even_remaining = -1;
			int target_odd_remaining = -1;

			pthread_mutex_lock(&state_lock);
			all_started = true;

			if (odd_waiting > 0 && even_waiting == 0 && odd_remaining > 0) {
				wake_odd = 1;
				target_odd_remaining = odd_remaining - 1;
			} else if (even_waiting > 0 && odd_waiting == 0 && even_remaining > 0) {
				wake_even = 1;
				target_even_remaining = even_remaining - 1;
			}
			pthread_mutex_unlock(&state_lock);

			if (wake_even) {
				sem_post(&even_sem);
				pthread_mutex_lock(&state_lock);
				while (even_remaining > target_even_remaining) {
					pthread_cond_wait(&finish_cond, &state_lock);
				}
				pthread_mutex_unlock(&state_lock);
			} else if (wake_odd) {
				sem_post(&odd_sem);
				pthread_mutex_lock(&state_lock);
				while (odd_remaining > target_odd_remaining) {
					pthread_cond_wait(&finish_cond, &state_lock);
				}
				pthread_mutex_unlock(&state_lock);
			}
		}

		pthread_create(&thread->handle, NULL, threadRun, thread);

		//wait until the last thread logs its start before continuing
		if (i == threadCount - 1) {
			pthread_mutex_lock(&state_lock);
			while (thread->state == 0) {
				pthread_cond_wait(&start_cond, &state_lock);
			}
			pthread_mutex_unlock(&state_lock);
		}
	}

	//wait for all threads to finish
	for (int i = 0; i < threadCount; i++) {
		pthread_join(threads[i].handle, NULL);
	}

	//cleanup
	sem_destroy(&even_sem);
	sem_destroy(&odd_sem);
	sem_destroy(&mutex);
	pthread_mutex_destroy(&state_lock);
	pthread_cond_destroy(&start_cond);
	pthread_cond_destroy(&finish_cond);

	//free threads array allocated in readFile() before exiting
	free(threads);

	return 0;
}

int readFile(char* fileName, Thread** threads){
	FILE* file = fopen(fileName, "r");
	if(file == NULL){
		printf("Error opening file.\n");
		return -1;
	}

	char line[100];
	int count = 0;

	while(fgets(line, sizeof(line), file)){
		count++;
	}
	rewind(file);

	*threads = (Thread*)malloc(count * sizeof(Thread));
	if(*threads == NULL){
		printf("Memory allocation failed.\n");
		fclose(file);
		return -1;
	}

	int index = 0;
	while(fgets(line, sizeof(line), file)){
		char* token = strtok(line, ";\r\n");
		if(token == NULL){
			printf("Error: malformed line in input file.\n");
			fclose(file);
			free(*threads);
			return -1;
		}
		strncpy((*threads)[index].tid, token, 4);
		(*threads)[index].tid[3] = '\0'; //ensure null-termination

		token = strtok(NULL, ";\r\n");
		if(token == NULL){
			printf("Error: missing start time in input file.\n");
			fclose(file);
			free(*threads);
			return -1;
		}
		(*threads)[index].startTime = (unsigned int)atoi(token);
		(*threads)[index].state = 0;

		index++;				
	}

	fclose(file);

	return count;
}

//helper function
void sortThreadsByStartTime(Thread* threads, int count){
	for(int i = 0; i < count - 1; i++){
		for(int j = 0; j < count - i - 1; j++){
			if(threads[j].startTime > threads[j + 1].startTime){
				Thread temp = threads[j];
				threads[j] = threads[j + 1];
				threads[j + 1] = temp;
			}
		}
	}
}

void logStart(char* tID)//do not change this method; you can use this method as per your desire
{
	printf("[%ld] New Thread with ID %s is started.\n", getCurrentTime(), tID);
}

void logFinish(char* tID)//do not change this method; you can use this method as per your desire
{
	printf("[%ld] Thread with ID %s is finished.\n", getCurrentTime(), tID);
}

void* threadRun(void* t){
	Thread* thread = (Thread*)t;
	logStart(thread->tid);

	pthread_mutex_lock(&state_lock);
	thread->state = 1;
	pthread_cond_broadcast(&start_cond);
	pthread_mutex_unlock(&state_lock);

	//get parity of the thread based on its ID
	int y = thread->tid[2] - '0';
    int my_parity = y % 2; // 0=even, 1=odd
	bool woke_from_parity_sem = false;
	
	while (1) {
        pthread_mutex_lock(&state_lock);

        //first thread ever, or correct parity
        if (last_parity == -1 || last_parity != my_parity) {
            // it's my turn - grab the mutex and go
            pthread_mutex_unlock(&state_lock);
            sem_wait(&mutex);
            break;
        }

        //wrong parity but check starvation condition
        int opposite_remaining = (my_parity == 0) ? odd_remaining : even_remaining;
		int same_waiting = (my_parity == 0) ? even_waiting : odd_waiting;
		if (all_started && opposite_remaining == 0 && (same_waiting == 0 || woke_from_parity_sem)) {
            // no opposite threads left, starvation escape
            pthread_mutex_unlock(&state_lock);
            sem_wait(&mutex);
            break;
        }

        //must wait - release state_lock and block on my semaphore
		if (my_parity == 0) {
			even_waiting++;
			pthread_mutex_unlock(&state_lock);
            sem_wait(&even_sem);
			pthread_mutex_lock(&state_lock);
			even_waiting--;
		} else {
			odd_waiting++;
			pthread_mutex_unlock(&state_lock);
            sem_wait(&odd_sem);
			pthread_mutex_lock(&state_lock);
			odd_waiting--;
		}
		woke_from_parity_sem = true;
		pthread_mutex_unlock(&state_lock);
        //after waking up, loop again to re-check
    }

	//critical section starts here, it has only the following printf statement
	printf("[%ld] Thread %s is in its critical section\n", getCurrentTime(), thread->tid);

	//critical section ends here
	pthread_mutex_lock(&state_lock);
    last_parity = my_parity;
    if (my_parity == 0) even_remaining--;
    else odd_remaining--;
	pthread_cond_broadcast(&finish_cond);

    int opposite_remaining = (my_parity == 0) ? odd_remaining : even_remaining;
    int same_remaining = (my_parity == 0) ? even_remaining : odd_remaining;
	int opposite_waiting = (my_parity == 0) ? odd_waiting : even_waiting;
	int same_waiting = (my_parity == 0) ? even_waiting : odd_waiting;

    pthread_mutex_unlock(&state_lock);

    //wake up a waiting thread of opposite parity
	if (opposite_remaining > 0 && opposite_waiting > 0) {
        if (my_parity == 0)
            sem_post(&odd_sem);
        else
            sem_post(&even_sem);
    } 
	else if (all_started && opposite_remaining == 0 && same_remaining > 0 && same_waiting > 0) {
        //wake same parity
        if (my_parity == 0)
            sem_post(&even_sem);
        else
            sem_post(&odd_sem);
    }

	sem_post(&mutex); //release critical section for next thread

	logFinish(thread->tid);
	return NULL;
}

void startClock()//do not change this method
{
	programClock = time(NULL);
}

long getCurrentTime()//invoke this method whenever you want to check how much time units passed
//since you invoked startClock()
{
	time_t now;
	now = time(NULL);
	return now-programClock;
}