#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "sharedMemory.h"

/*#######################################################*/
/*                   Car Park Constants                  */
/*#######################################################*/

#define ENTRANCES 5
#define EXITS 5
#define LEVELS 5
#define LEVEL_CAPACITY 20
#define LPRS 15
#define BOOM_GATES 10
#define INFO_SIGNS 5
#define TEMP_SENSORS 5
#define TEMP_ALARMS 5
#define CAR_PARK_ACCESS_FALSE 'X'
#define CAR_PARK_FULL 'F'

/*#######################################################*/
/*                    Simulator Timings                  */
/*#######################################################*/
// Car
#define MAX_TIME_CAR_GENERATION 100
#define MIN_TIM_CAR_GENERATION 100000
#define MAX_TIM_CAR_GENERATION 10000000
#define WAIT_TIME_CAR_ENTRY_LPR 2
#define MOVING_TIME_CAR_TO_PARKING 10
#define MIN_TIME_CAR_PARKED 100000
#define MAX_TIME_CAR_PARKED 10000000
#define MOVING_TIME_CAR_TO_EXIT

// Boom Gate
#define TIME_GATE_OPEN_STATE 10000
#define TIME_GATE_CLOSED_STATE 10000

// Temperature
#define MAX_TIME_TEMPERATURE_GENERATION 5

/*#######################################################*/
/*            Mutexs and Condition Variables             */
/*#######################################################*/
pthread_mutex_t mutex_timings = PTHREAD_MUTEX_INITIALIZER;
pthread_condattr_t cond_var_att;
pthread_mutexattr_t mutex_att;

// The three processes communicate via a shared memory segment named PARKING
/*
The PARKING segment is 2920 bytes in size. The simulator needs to create this
segment when it is first started. The simulator first unlinks any previously created
shared memory segments.
*/

/*#######################################################*/
/*               Functions for Shared Memory             */
/*#######################################################*/

struct car_park_shm *Create_Shared_Memory(const char *name){
    shm_unlink(name);
    int shm_fd;
    if ((shm_fd = shm_open(name, O_RDWR | O_CREAT, 0666)) < 0)
    {
        perror("shm_open");
        exit(1);
    }

    ftruncate(shm_fd, sizeof(struct car_park_shm));
    struct car_park_shm *shm = mmap(NULL, sizeof(struct car_park_shm), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    printf("Size of shared memory: %ld\n", sizeof(struct car_park_shm));
    return shm;
}

/*#######################################################*/
/*                  Functions for Cars                   */
/*#######################################################*/

char *Create_Random_License_Plate(){
    char license_plate[8];
    int license_plate_randomiser_max = 100;
    pthread_mutex_lock(&mutex_timings);
    int car_plate_randomiser = ((rand() % license_plate_randomiser_max) + 1);
    printf("Random number for car plate generator: %d \n", car_plate_randomiser);
    pthread_mutex_unlock(&mutex_timings);

    if (car_plate_randomiser > 50){
        FILE *file;

        file = fopen("plates.txt", "r");

        if (file == NULL)
        {
            printf("Error opening file.\n");
            exit(1);
        }

        // line will keep track of the number of lines read so far from the file
        int lines = 0;
        while (fgets(license_plate, sizeof license_plate, file) != NULL && lines != car_plate_randomiser)
        {
            lines++;
        }
        license_plate[6] = '\0';
        printf("license plate selected from plate.text: %s\n",license_plate);
        fclose(file);
    } else {
        int i;
        for(i = 0; i < 3; i++) {
		    license_plate[i] = (char)((rand() % 26) + 65);
            }
        for(i = 3; i < 6; i++) {
        license_plate[i] = (char)((rand() % 10) + 48);
	    }
        license_plate[6] = '\0';
        printf("Plate randomly generated %s\n", license_plate);
    }
    char *temporary_plate = (char *)malloc(sizeof(char) * 8);
    strcpy(temporary_plate, license_plate);
    return temporary_plate;
}

// Simulator will make car wait in parking spot for an amount of time between 100-10000ms.
int Car_Parking_Time(){
    // times in milliseconds
    pthread_mutex_lock(&mutex_timings);
    srand(time(0));
    int car_parked_time = (rand() % (MAX_TIME_CAR_PARKED - MIN_TIME_CAR_PARKED + 1)) + MIN_TIME_CAR_PARKED;
    pthread_mutex_unlock(&mutex_timings);
    return car_parked_time;
}

void* Navigate_Car(void* data){
    struct car_park_shm *parking_memory = (struct car_park_shm *)data;
    // Step 1: Assign a car a randomly generated license plate.
    char *license_plate = Create_Random_License_Plate();

    // Step 2: Pick an entrance for the car to apprear
    pthread_mutex_lock(&mutex_timings);
    srand(time(0));
    int chosen_entrance = (rand() % (ENTRANCES));
    printf("Car entering Entry: %d\n", chosen_entrance);
    pthread_mutex_unlock(&mutex_timings);

    // Step 3: Trigger the LPR for that entrance.
    pthread_mutex_lock(&parking_memory->entries[chosen_entrance].Entry_LPR_mutex);
    strcpy(parking_memory->entries[chosen_entrance].plate, license_plate);
    printf("state %s", parking_memory->entries[chosen_entrance].plate);
    if(pthread_cond_broadcast(&parking_memory->entries[chosen_entrance].Entry_LPR_cond) != 0){
        perror("pthread_cond_broadcast was not successful");
    }
    pthread_cond_wait(&parking_memory->entries[chosen_entrance].Entry_LPR_cond, &parking_memory->entries[chosen_entrance].Entry_LPR_mutex);
    pthread_mutex_unlock(&parking_memory->entries[chosen_entrance].Entry_LPR_mutex);

    // Step 4: Validate the license plate of the car.
    pthread_mutex_lock(&parking_memory->entries[chosen_entrance].Entry_BG_mutex);
    strcpy(parking_memory->entries[chosen_entrance].plate, license_plate);
    if(pthread_cond_broadcast(&parking_memory->entries[chosen_entrance].Entry_LPR_cond) != 0){
        perror("pthread_cond_broadcast was not successful");
    }
    pthread_cond_wait(&parking_memory->entries[chosen_entrance].Entry_LPR_cond, &parking_memory->entries[chosen_entrance].Entry_LPR_mutex);
    pthread_mutex_unlock(&parking_memory->entries[chosen_entrance].Entry_IS_mutex);
    

    // Don't allow car in to car park if license plate not valid or car park is full
    if (parking_memory->entries[chosen_entrance].information == CAR_PARK_ACCESS_FALSE){
        printf("Access Denied: License plate %s invalid\n", license_plate);
        pthread_exit(NULL);
    }

    else if (parking_memory->entries[chosen_entrance].information == CAR_PARK_FULL){
        printf("Access Denied: Car park is full");
        pthread_exit(NULL);
    }
    else{
        // Step 5: Boom gate operating. Car waits until fully open.
        printf("Access Granted: License plate %s valid\n", license_plate);

        // Step 6: The level LPR on the level the car entered is triggered.
        printf("Car with license plate %s entering level %d\n", license_plate, parking_memory->entries[chosen_entrance].information);

        // Step 7: Boom gate operating. Cars wait until gate is fully closed and then open again.
        pthread_mutex_lock(&mutex_timings);
        srand(time(0));
        int chosen_exit = (rand() % (EXITS));
        printf("Exit: %d\n", chosen_exit);
        pthread_mutex_unlock(&mutex_timings);

        // Step 8 Operate Exit LPR
        pthread_mutex_lock(&parking_memory->exits[chosen_exit].Exit_LPR_mutex);
        strcpy(parking_memory->exits[chosen_exit].plate, license_plate);
        if(pthread_cond_broadcast(&parking_memory->exits[chosen_exit].Exit_LPR_cond) != 0){
            perror("pthread_cond_broadcast was not successful");
        }
        pthread_mutex_unlock(&parking_memory->exits[chosen_exit].Exit_LPR_mutex);
    }
    return 0;
}

void* Operate_Entry_Gates(void *data){
    struct entrance *parking_memory_entry = (struct entrance *)data;
    for(;;){
        if(parking_memory_entry->state == 'R'){
            pthread_mutex_lock(&parking_memory_entry->Entry_BG_mutex);
            usleep(TIME_GATE_OPEN_STATE);
            parking_memory_entry->state = 'O';
            pthread_cond_broadcast(&parking_memory_entry->Entry_BG_cond);
            printf("Entry Boom Gate now open");
            pthread_mutex_unlock(&parking_memory_entry->Entry_BG_mutex);
        }
        if(parking_memory_entry->state == 'L'){
            pthread_mutex_lock(&parking_memory_entry->Entry_BG_mutex);
            usleep(TIME_GATE_CLOSED_STATE);
            parking_memory_entry->state = 'C';
            pthread_cond_broadcast(&parking_memory_entry->Entry_BG_cond);
            printf("Entry Boom Gate now closed");
            pthread_mutex_unlock(&parking_memory_entry->Entry_BG_mutex);
        }
    }
}

void* Operate_Exit_Gates(void *data){
    struct exit *parking_memory_exit = (struct exit *)data;
    for(;;){
        if(parking_memory_exit->state == 'R'){
            pthread_mutex_lock(&parking_memory_exit->Exit_BG_mutex);
            usleep(TIME_GATE_OPEN_STATE);
            parking_memory_exit->state = 'O';
            pthread_cond_broadcast(&parking_memory_exit->Exit_BG_cond);
            printf("Exit Boom Gate now open");
            pthread_mutex_unlock(&parking_memory_exit->Exit_BG_mutex);
        }
        if(parking_memory_exit->state == 'L'){
            pthread_mutex_lock(&parking_memory_exit->Exit_BG_mutex);
            usleep(TIME_GATE_CLOSED_STATE);
            parking_memory_exit->state = 'C';
            pthread_cond_broadcast(&parking_memory_exit->Exit_BG_cond);
            printf("Exit Boom Gate now closed");
            pthread_mutex_unlock(&parking_memory_exit->Exit_BG_mutex);
        }
    }
}

void* Generate_Temperature(void *data){
    for(;;){
        struct level *parking_memory_level = (struct level *)data;
        int temperature = (rand() % (70 - 25 + 1) + 25);
        printf("random temperature generated %d\n", temperature);
        parking_memory_level->temperature = temperature;
        return 0;
    }
}

int main()
{
    system("clear");
    printf("Simulator Initialising\n");
    struct car_park_shm *parking_memory = (struct car_park_shm *)Create_Shared_Memory("PARKING");

    pthread_mutexattr_init(&mutex_att);
    pthread_condattr_init(&cond_var_att);
    pthread_mutexattr_setpshared(&mutex_att, PTHREAD_PROCESS_SHARED);
    pthread_condattr_setpshared(&cond_var_att, PTHREAD_PROCESS_SHARED);

    /*#######################################################*/
    /*                  Memory Initialisation                */
    /*#######################################################*/
    for(int i = 0; i < (ENTRANCES); i++){
        pthread_mutex_init(&parking_memory->entries[i].Entry_LPR_mutex, &mutex_att);
        pthread_cond_init(&parking_memory->entries[i].Entry_LPR_cond, &cond_var_att);

        pthread_mutex_init(&parking_memory->entries[i].Entry_BG_mutex, &mutex_att);
        pthread_cond_init(&parking_memory->entries[i].Entry_BG_cond, &cond_var_att);

        pthread_mutex_init(&parking_memory->entries[i].Entry_IS_mutex, &mutex_att);
        pthread_cond_init(&parking_memory->entries[i].Entry_IS_Cond, &cond_var_att);
    }

    for(int i = 0; i < (EXITS); i++){
        pthread_mutex_init(&parking_memory->exits[i].Exit_LPR_mutex, &mutex_att);
        pthread_cond_init(&parking_memory->exits[i].Exit_LPR_cond, &cond_var_att);

        pthread_mutex_init(&parking_memory->exits[i].Exit_BG_mutex, &mutex_att);
        pthread_cond_init(&parking_memory->exits[i].Exit_BG_cond, &cond_var_att);
    }

    for(int i = 0; i < (LEVELS); i++){
        pthread_mutex_init(&parking_memory->levels[i].Level_LPR_mutex, &mutex_att);
        pthread_cond_init(&parking_memory->levels[i].Level_LPR_cond, &cond_var_att);
    }
    printf("Pausing to ensure manager has shared memory\n");
    sleep(5);

    /*
    Create Threads for use throughout the simulator
    */
   /*#######################################################*/
    /*              Entrance Boom Gate Threads               */
    /*#######################################################*/
    pthread_t entrance_gate_thread;
    for (int i = 0; i < ENTRANCES; i++){
        parking_memory->entries[i].state = 'C';
        if(pthread_create(&entrance_gate_thread, NULL, (void *) Operate_Entry_Gates, &parking_memory->entries[i]) != 0){
            perror("Failed to create entry boom gate thread");
        };
    }

    /*#######################################################*/
    /*                Exit Boom Gate Threads                 */
    /*#######################################################*/
    pthread_t exit_gate_thread;
    for (int i = 0; i < EXITS; i++){
        parking_memory->exits[i].state = 'C';
        if(pthread_create(&exit_gate_thread, NULL, (void *) Operate_Exit_Gates, &parking_memory->exits[i]) != 0){
            perror("Failed to create exit boom gate thread");
        };
    }

    /*#######################################################*/
    /*                  Temperature Threads                  */
    /*#######################################################*/
    pthread_t temperature_thread;
    for (int i = 0; i < LEVELS; i++){
        if(pthread_create(&temperature_thread, NULL, (void *) Generate_Temperature, &parking_memory->levels[i]) != 0){
            perror("Failed to create exit boom gate thread");
        };
    }

    /*#######################################################*/
    /*                      Car Threads                      */
    /*#######################################################*/
    int car_seed = 25;
    pthread_t car_thread;
    for (int i = 0; i < car_seed; i++){
        pthread_create(&car_thread, NULL, Navigate_Car, parking_memory);

        // Simulator will make car wait in parking spot for an amount of time between 100-10000ms (times in milliseconds).
        pthread_mutex_lock(&mutex_timings);
        srand(time(0));
        int generate_car_time = (rand() % (MAX_TIM_CAR_GENERATION - MIN_TIM_CAR_GENERATION + 1) + MIN_TIM_CAR_GENERATION);
        pthread_mutex_unlock(&mutex_timings);
        usleep(generate_car_time);
    }

    /*#######################################################*/
    /*                     Join Threads                      */
    /*#######################################################*/
    for (int i = 0; i < car_seed; i++){
        pthread_join(car_thread, NULL);
    }

    for (int i = 0; i < ENTRANCES; i++){
        pthread_join(entrance_gate_thread, NULL);
    }

    for (int i = 0; i < EXITS; i++){
        pthread_join(exit_gate_thread, NULL);
    }

    for (int i = 0; i < LEVELS; i++){
        pthread_join(temperature_thread, NULL);
    }
    
    return 0;
}