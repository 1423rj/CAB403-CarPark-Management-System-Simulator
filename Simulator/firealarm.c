#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include "sharedMemory.h"

int shm_fd;
volatile void *shm;

int alarm_active = 0;
pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_condvar = PTHREAD_COND_INITIALIZER;

#define LEVELS 5
#define ENTRANCES 5
#define EXITS 5

#define MEDIAN_WINDOW 5
#define TEMPCHANGE_WINDOW 30

// shared memory PARKING
#define PARKING_MEMORY_SIZE 2920

struct tempnode {
	int temperature;
	struct tempnode *next;
};

struct tempnode *deletenodes(struct tempnode *templist, int after)
{
	if (templist->next) {
		templist->next = deletenodes(templist->next, after - 1);
	}
	if (after <= 0) {
		free(templist);
		return NULL;
	}
	return templist;
}
int compare(const void *first, const void *second)
{
	return *((const int *)first) - *((const int *)second);
}

void *tempmonitor(void* data)
{
	struct level *parking_memory_level = (struct level *)data;
	struct tempnode *templist = NULL, *newtemp, *medianlist = NULL, *oldesttemp;
	int count, temp, mediantemp, hightemps;
	
	for (;;) {
		// Calculate address of temperature sensor
		temp = parking_memory_level->temperature;
		
		// Add temperature to beginning of linked list
		// Malloc is needed here due to requiring a new struct for tracking temperatures
		newtemp = malloc(sizeof(struct tempnode));
		newtemp->temperature = temp;
		newtemp->next = templist;
		templist = newtemp;
		
		// Delete nodes after 5th
		deletenodes(templist, MEDIAN_WINDOW);
		
		// Count nodes
		count = 0;
		for (struct tempnode *t = templist; t != NULL; t = t->next) {
			count++;
		}
		
		if (count == MEDIAN_WINDOW) { // Temperatures are only counted once we have 5 samples
			// Malloc is needed here to dynamically allocate memory to sorttemp integer
			int *sorttemp = malloc(sizeof(int) * MEDIAN_WINDOW);
			count = 0;
			for (struct tempnode *t = templist; t != NULL; t = t->next) {
				sorttemp[count++] = t->temperature;
			}
			qsort(sorttemp, MEDIAN_WINDOW, sizeof(int), compare);
			mediantemp = sorttemp[(MEDIAN_WINDOW - 1) / 2];
			
			// Add median temp to linked list
			// Malloc is needed here due to requiring the size of the struct tempnode,
			// which is not designated in memory
			newtemp = malloc(sizeof(struct tempnode));
			newtemp->temperature = mediantemp;
			newtemp->next = medianlist;
			medianlist = newtemp;
			
			// Delete nodes after 30th
			deletenodes(medianlist, TEMPCHANGE_WINDOW);
			
			// Count nodes
			count = 0;
			hightemps = 0;
			
			for (struct tempnode *t = medianlist; t != NULL; t = t->next) {
				// Temperatures of 58 degrees and higher are a concern
				if (t->temperature >= 58) hightemps++;
				// Store the oldest temperature for rate-of-rise detection
				oldesttemp = t;
				count++;
			}
			
			if (count == TEMPCHANGE_WINDOW) {
				// If 90% of the last 30 temperatures are >= 58 degrees,
				// this is considered a high temperature. Raise the alarm
				if (hightemps >= TEMPCHANGE_WINDOW * 0.9)
					alarm_active = 1;
				
				// If the newest temp is >= 8 degrees higher than the oldest
				// temp (out of the last 30), this is a high rate-of-rise.
				// Raise the alarm
				if (templist->temperature - oldesttemp->temperature >= 8)
					alarm_active = 1;
			}
		}
		
		usleep(2000);
		
	}
}

void *open_entrance_boomgate(void *data)
{
	struct entrance *car_park_shm_entry = (struct entrance *)data;
	pthread_mutex_lock(&car_park_shm_entry->Entry_BG_mutex);
	for (;;) {
		if (car_park_shm_entry->state == 'C') {
			car_park_shm_entry->state  = 'R';
			pthread_cond_broadcast(&car_park_shm_entry->Entry_BG_cond);
		}
		if (car_park_shm_entry->state == 'O') {
		}
		pthread_cond_wait(&car_park_shm_entry->Entry_BG_cond, &car_park_shm_entry->Entry_BG_mutex);
	}
	pthread_mutex_unlock(&car_park_shm_entry->Entry_BG_mutex);
	
}

void *open_exit_boomgate(void *data)
{
	struct exit *car_park_shm_exit = (struct exit *)data;
	pthread_mutex_lock(&car_park_shm_exit->Exit_BG_mutex);
	for (;;) {
		if (car_park_shm_exit->state == 'C') {
			car_park_shm_exit->state  = 'R';
			pthread_cond_broadcast(&car_park_shm_exit->Exit_BG_cond);
		}
		if (car_park_shm_exit->state == 'O') {
		}
		pthread_cond_wait(&car_park_shm_exit->Exit_BG_cond, &car_park_shm_exit->Exit_BG_mutex);
	}
	pthread_mutex_unlock(&car_park_shm_exit->Exit_BG_mutex);
	
}

struct car_park_shm *Open_Shared_Memory(const char *name){
    int shm_fd;
    if ((shm_fd = shm_open(name, O_RDWR , 0666)) < 0)
    {
        perror("shm_open\n");
        exit(1);
    }

    ftruncate(shm_fd, sizeof(struct car_park_shm));
    struct car_park_shm *shm = mmap(NULL, sizeof(struct car_park_shm), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED)
    {
        perror("mmap\n");
        exit(1);
    }

    printf("Size of shared memory: %ld\n", sizeof(struct car_park_shm));
    return shm;
}

int main()
{
	struct car_park_shm *parking_memory = (struct car_park_shm *)Open_Shared_Memory("PARKING");
	
	pthread_t Temp_Monitor_Thread;
	for (int i = 0; i < (LEVELS); i++){
        if(pthread_create(&Temp_Monitor_Thread, NULL, tempmonitor, &parking_memory->levels[i]) != 0){ // changed the fire 
            perror("Failed to create entry_IS_thread");
        }
    }

	for (;;) {
		if (alarm_active) {
			fprintf(stderr, "*** ALARM ACTIVE ***\n");
	
			// Handle the alarm system and open boom gates
			// Activate alarms on all levels
			for (int i = 0; i < LEVELS; i++) {
				parking_memory->levels->alarm_state = 1;
			}
			
			// Open up all boom gates
			pthread_t Entry_BG_Thread;
			for (int i = 0; i < (ENTRANCES); i++){
				if(pthread_create(&Entry_BG_Thread, NULL, open_entrance_boomgate, &parking_memory->entries[i]) != 0){
					perror("Failed to create entry_bg_thread");
				}
			}

			pthread_t Exit_BG_Thread;
			for (int i = 0; i < (EXITS); i++){
				if(pthread_create(&Exit_BG_Thread, NULL, open_exit_boomgate, &parking_memory->exits[i]) != 0){
					perror("Failed to create entry_bg_thread");
				}
			}
			
			// Show evacuation message on an endless loop
			for (;;) {
				char *evacmessage = "EVACUATE ";
				for (char *p = evacmessage; *p != '\0'; p++) {
					for (int i = 0; i < ENTRANCES; i++) {
						pthread_mutex_lock(&parking_memory->entries->Entry_IS_mutex);
						parking_memory->entries->information = *p;
						if(pthread_cond_broadcast(&parking_memory->entries->Entry_IS_Cond) != 0){
							perror("broadcast failed");
						};
						pthread_mutex_unlock(&parking_memory->entries->Entry_IS_mutex);
					}
					usleep(20000);
				}
			}

			for (int i = 0; i < ENTRANCES; i++) {
				pthread_join(Entry_BG_Thread, NULL);
			}
			
			for (int i = 0; i < EXITS; i++) {
				pthread_join(Exit_BG_Thread, NULL);
			}
		}
		usleep(1000);
	}

	for (int i = 0; i < LEVELS; i++) {
		pthread_join(Temp_Monitor_Thread, NULL);
	}
}
