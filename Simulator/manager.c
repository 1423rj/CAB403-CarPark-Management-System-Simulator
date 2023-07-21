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
#include "sharedMemory.h"
#include "hashTable.h"

/*#######################################################*/
/*                  Shared Memory Sizes                  */
/*#######################################################*/

#define PARKING_MEMORY_SEGMENT_SIZE 2920
#define LPR_GATE_SIGN_MEM_SIZE 96
#define ENTRY_MEM_SIZE 288
#define EXIT_MEM_SIZE 192
#define LEVEL_MEM_SIZE 104
#define SENSOR_MEM_SIZE 2
#define ALARM_MEM_SIZE 1

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
#define CAR_PARK_ACCESS_FALSE 88
#define CAR_PARK_FULL 70
#define CLOCK_PRECISION 1E9

/*#######################################################*/
/*                    Simulator Timings                  */
/*#######################################################*/
// Car
#define MAX_TIME_CAR_GENERATION 100
#define WAIT_TIME_CAR_ENTRY_LPR 2
#define MOVING_TIME_CAR_TO_PARKING 10
#define MAX_TIME_CAR_PARKED 10000
#define MOVING_TIME_CAR_TO_EXIT

// Boom Gates
#define WAIT_TIME_GATE_BEGIN_CLOSING 20000
#define INDIVIDUAL_PROCESS_TIME 5000
// Temperature
#define MAX_TIME_TEMPERATURE_GENERATION 5

// constants for maximum number of lines in plates.txt
// maximum number of characters per licence plate
#define MAX_LINES 100
#define MAX_LEN 8

// double to track billing of cars using the car park
double billing;

// integer array to track total amount of cars in each level of the car park
int level_capacity[LEVELS];

// integer to track total amount of cars in car park
int total_cars = 0;

// Global hash table for 
htab_t plate_hash_table;

struct Car{
    char plate[7];
    int value;
    int level;
    int car_status;
    struct timespec time_car_entered;
    time_t car_time_elapsed;
};

struct Car cars[100];
int cars_indexer = 0;

// Add to the car array
void add_array(int designated_level, char* designated_plate){
    strcpy(cars[cars_indexer].plate, designated_plate);
    struct timespec arrivingTime;
    clock_gettime(CLOCK_REALTIME, &arrivingTime);
    cars[cars_indexer].time_car_entered = arrivingTime;
    cars[cars_indexer].level = designated_level + 1;
    cars[cars_indexer].car_status = 1;
    level_capacity[designated_level] = level_capacity[designated_level] + 1;
    total_cars = total_cars + 1;
    cars_indexer = cars_indexer + 1;
}

// Remove from the car
void minus_array(int cars_indexer_exit, int designated_level){
    FILE *f = fopen("billing.txt", "a");
    struct timespec displayTime;
    clock_gettime(CLOCK_REALTIME, &displayTime);
    double elapsed = 0;
    elapsed = ((displayTime.tv_sec - cars[cars_indexer_exit].time_car_entered.tv_sec) + ((displayTime.tv_nsec - cars[cars_indexer_exit].time_car_entered.tv_nsec) / CLOCK_PRECISION));
    fprintf(f, "License Plate: %s, Total Time: %.2f seconds, Total Billed: %.2f\n", cars[cars_indexer_exit].plate, elapsed, elapsed*50);
    cars[cars_indexer_exit].time_car_entered.tv_sec =  -1; 
    cars[cars_indexer_exit].time_car_entered.tv_nsec =  0; 
    cars[cars_indexer_exit].level = 88;
    cars[cars_indexer_exit].car_status = 0;
    level_capacity[designated_level] = level_capacity[designated_level] - 1;
    billing = billing + (elapsed *50);
    fclose(f);
}

// Check global variable total_cars to see if car park is full
int Is_Car_Park_Full(){
    int car_park_capacity = LEVELS * LEVEL_CAPACITY;
    // Car Park has space for next car
    if (total_cars + 1 < car_park_capacity){
        return 0;
    }
    return 1;
}

// Check all levels within the car park to find an empty level.
// Return the first empty level as the designated level.
int Find_Level_Not_Full(){
    int designated_level;
    for(int i = 0; i < LEVELS; i++){
        if(level_capacity[i] < 20){
            designated_level = i;
        }
    }
    return designated_level;
}

// Control the status of all LPRs within the car park.
// cars_entered is the total amount of cars entered in to the car park
void* Entry_LPR_Status(void* data){
    struct entrance *car_park_shm_entry = (struct entrance *)data;
    //"Entrance LPR";
    for(;;){
        //LPR waiting on car;
        pthread_mutex_lock(&car_park_shm_entry->Entry_LPR_mutex);
        pthread_cond_wait(&car_park_shm_entry->Entry_LPR_cond, &car_park_shm_entry->Entry_LPR_mutex);

        char car_plate[7];
        memccpy(car_plate, &car_park_shm_entry->plate, 0, 6);
        car_plate[6] = '\0';
        pthread_mutex_unlock(&car_park_shm_entry->Entry_LPR_mutex);

        // LPR has noticed car at entrance with license plate
        // Car waiting at entrance
        if(car_park_shm_entry->plate != NULL){
            //Checking car park capacity and if license plate is valid;
            if((Is_Car_Park_Full() == 0) && htab_find(&plate_hash_table, car_park_shm_entry->plate)){
                // Find a level that is not full
                int designated_level = Find_Level_Not_Full();

                //Adding car to car park array
                add_array(designated_level, car_plate);

                // Update the information sign
                pthread_mutex_lock(&car_park_shm_entry->Entry_IS_mutex);
                car_park_shm_entry->information = designated_level + 1;
                pthread_cond_broadcast(&car_park_shm_entry->Entry_IS_Cond);
                pthread_mutex_unlock(&car_park_shm_entry->Entry_IS_mutex);
            }

            // Car park is full
            else if(Is_Car_Park_Full() == 1){
                pthread_mutex_lock(&car_park_shm_entry->Entry_IS_mutex);
                //Access Denied: Car park is full
                car_park_shm_entry->information = CAR_PARK_FULL;
                pthread_cond_broadcast(&car_park_shm_entry->Entry_IS_Cond);
                pthread_mutex_unlock(&car_park_shm_entry->Entry_IS_mutex);
            }

            else{
                pthread_mutex_lock(&car_park_shm_entry->Entry_IS_mutex);
                // License plate not in license plate hash table
                // Access Denied: Car license plate not valid
                car_park_shm_entry->information = CAR_PARK_ACCESS_FALSE;
                pthread_cond_broadcast(&car_park_shm_entry->Entry_IS_Cond);
                pthread_mutex_unlock(&car_park_shm_entry->Entry_IS_mutex);
            }
        }
    }
}

// Control the status of all Information Signs within the car park.
// Information_type must be 1 till 9.
// Level 1 ('1').
// Level 2 ('2').
// Level 3 ('3').
// Level 4 ('4').
// Level 5 ('5').
// Car Park Full ('F')
// Unrecognised License Plate ('X')
// Fire Detected ('E', 'V', 'A', 'C', 'U', 'A', 'T', 'E', ' ')
void* Information_Sign_Display(void* data){
    struct entrance *car_park_shm_entry = (struct entrance *)data;
    //Entrance Information Sign

    for(;;){
        //Waiting on Information Sign Broadcast
        pthread_cond_wait(&car_park_shm_entry->Entry_IS_Cond, &car_park_shm_entry->Entry_IS_mutex);
        //Sign done waiting
        if( car_park_shm_entry->information == CAR_PARK_ACCESS_FALSE){
            //Sign: License Plate not recognised
            car_park_shm_entry->information = 'X';
        }
        if( car_park_shm_entry->information == CAR_PARK_FULL){
            //Sign: Car Park Full
            car_park_shm_entry->information = 'F';

        }
        if( car_park_shm_entry->information == 5){
            car_park_shm_entry->information = '5';
            pthread_mutex_lock(&car_park_shm_entry->Entry_IS_mutex);
            car_park_shm_entry->state = 'C';
            pthread_cond_broadcast(&car_park_shm_entry->Entry_BG_cond);
            pthread_mutex_unlock(&car_park_shm_entry->Entry_IS_mutex);
        }
        
        if( car_park_shm_entry->information == 4){
            car_park_shm_entry->information = '4';
            pthread_mutex_lock(&car_park_shm_entry->Entry_IS_mutex);
            car_park_shm_entry->state = 'C';
            pthread_cond_broadcast(&car_park_shm_entry->Entry_BG_cond);
            pthread_mutex_unlock(&car_park_shm_entry->Entry_IS_mutex);
        }

        if( car_park_shm_entry->information == 3){
            car_park_shm_entry->information = '3';
            pthread_mutex_lock(&car_park_shm_entry->Entry_IS_mutex);
            car_park_shm_entry->state = 'C';
            pthread_cond_broadcast(&car_park_shm_entry->Entry_BG_cond);
            pthread_mutex_unlock(&car_park_shm_entry->Entry_IS_mutex);
        }

        if( car_park_shm_entry->information == 2){
            car_park_shm_entry->information = '2';
            pthread_mutex_lock(&car_park_shm_entry->Entry_IS_mutex);
            car_park_shm_entry->state = 'C';
            pthread_cond_broadcast(&car_park_shm_entry->Entry_BG_cond);
            pthread_mutex_unlock(&car_park_shm_entry->Entry_IS_mutex);
        }

        if( car_park_shm_entry->information == 1){
            car_park_shm_entry->information = '1';
            pthread_mutex_lock(&car_park_shm_entry->Entry_IS_mutex);
            car_park_shm_entry->state = 'C';
            pthread_cond_broadcast(&car_park_shm_entry->Entry_BG_cond);
            pthread_mutex_unlock(&car_park_shm_entry->Entry_IS_mutex);
        }
    }
}

// Operate the Exit LPR
void* Exit_LPR_Status(void* data){
    struct exit *car_park_shm_exit = (struct exit *)data;
    for(;;){
        pthread_mutex_lock(&car_park_shm_exit->Exit_LPR_mutex);
        pthread_cond_wait(&car_park_shm_exit->Exit_LPR_cond, &car_park_shm_exit->Exit_LPR_mutex);
        char car_plate[7];
        memccpy(car_plate, &car_park_shm_exit->plate, 0, 7);
        car_plate[7] = '\0';
        pthread_mutex_unlock(&car_park_shm_exit->Exit_LPR_mutex);

        if(car_park_shm_exit->plate != NULL){
            for(int i = 0; i < 100; i++){
                
                if( car_plate[0] == cars[i].plate[0] && car_plate[1] == cars[i].plate[1] && car_plate[2] == cars[i].plate[2] && car_plate[3] == cars[i].plate[3] && car_plate[4] == cars[i].plate[4] && car_plate[5] == cars[i].plate[5]){
                    minus_array(i, cars[i].level);
                }
            }
        }
    }
}

// Operate the Level LPR
void* Level_LPR_Status(void* data){
    struct level *car_park_shm_level = (struct level *)data;
    for(;;){
        pthread_mutex_lock(&car_park_shm_level->Level_LPR_mutex);
        pthread_cond_wait(&car_park_shm_level->Level_LPR_cond, &car_park_shm_level->Level_LPR_mutex);

        char car_plate[7];
        memccpy(car_plate, &car_park_shm_level->plate, 0, 6);
        car_plate[6] = '\0';
        pthread_mutex_unlock(&car_park_shm_level->Level_LPR_mutex);
    }
    return 0;
}

// Operate the Entry boom gate
void* operate_entry_gate(void* data){
    struct entrance *car_park_shm_entry = (struct entrance *)data;
    for(;;){
        if(car_park_shm_entry->state == 'C'){
            // Conduct the entire boom gate cycle for the entry gate
            // Closed -> Raising -> Open -> Lowering -> Closed
            // Only continue once the boom gate has reached closed state again
            pthread_mutex_lock(&car_park_shm_entry->Entry_BG_mutex);
            car_park_shm_entry->state = 'R';
            usleep(INDIVIDUAL_PROCESS_TIME);
            pthread_cond_broadcast(&car_park_shm_entry->Entry_BG_cond);
            pthread_mutex_unlock(&car_park_shm_entry->Entry_BG_mutex);
            usleep(WAIT_TIME_GATE_BEGIN_CLOSING);
        }
        if(car_park_shm_entry->state == 'O'){
            pthread_mutex_lock(&car_park_shm_entry->Entry_BG_mutex);
            car_park_shm_entry->state = 'L';
            pthread_cond_broadcast(&car_park_shm_entry->Entry_BG_cond);
            pthread_mutex_unlock(&car_park_shm_entry->Entry_BG_mutex);
        }    
        pthread_cond_wait(&car_park_shm_entry->Entry_BG_cond, &car_park_shm_entry->Entry_BG_mutex);
    }
}

// Operate the Exit boom gate
void* operate_exit_gate(void* data){
    struct exit *car_park_shm_exit = (struct exit *)data;
    for(;;){
        if(car_park_shm_exit->state == 'C'){
            // Conduct the entire boom gate cycle for the entry gate
            // Closed -> Raising -> Open -> Lowering -> Closed
            // Only continue once the boom gate has reached closed state again
            pthread_mutex_lock(&car_park_shm_exit->Exit_BG_mutex);
            car_park_shm_exit->state = 'R';
            usleep(INDIVIDUAL_PROCESS_TIME);
            car_park_shm_exit->state = 'O';
            pthread_cond_broadcast(&car_park_shm_exit->Exit_BG_cond);
            pthread_mutex_unlock(&car_park_shm_exit->Exit_BG_mutex);
            pthread_cond_wait(&car_park_shm_exit->Exit_BG_cond, &car_park_shm_exit->Exit_BG_mutex);
            usleep(WAIT_TIME_GATE_BEGIN_CLOSING);
            pthread_mutex_lock(&car_park_shm_exit->Exit_BG_mutex);
            car_park_shm_exit->state = 'L';
            pthread_cond_broadcast(&car_park_shm_exit->Exit_BG_cond);
            pthread_mutex_unlock(&car_park_shm_exit->Exit_BG_mutex);
            pthread_cond_wait(&car_park_shm_exit->Exit_BG_cond, &car_park_shm_exit->Exit_BG_mutex);
        }
    }
}

// Operate the Status Display
void* Status_Display(void* data){
    struct car_park_shm *parking_memory = (struct car_park_shm *)data;
    unsigned int display_refresh_time = 1000000;
    int test = 0;
    for(;;){
        if (test == 500){
            break;
        }
        printf("Level 1: %d/%d        Level 2: %d/%d        Level 3: %d/%d        Level 4: %d/%d        Level 5: %d/%d\n", 
                level_capacity[0], LEVEL_CAPACITY, 
                level_capacity[1], LEVEL_CAPACITY, 
                level_capacity[2], LEVEL_CAPACITY, 
                level_capacity[3], LEVEL_CAPACITY, 
                level_capacity[4], LEVEL_CAPACITY);
        printf("\n");
        printf("Level 1 LPR: %s        Level 2 LPR: %s        Level 3 LPR: %s        Level 4 LPR: %s        Level 5 LPR: %s\n", 
                parking_memory->levels[0].plate,
                parking_memory->levels[1].plate,
                parking_memory->levels[2].plate,
                parking_memory->levels[3].plate,
                parking_memory->levels[4].plate);
        printf("\n");
        printf("Info Sign 1: %c          Info Sign 2: %c          Info Sign 3: %c          Info Sign 4: %c          Info Sign 5: %c\n",
                parking_memory->entries[0].information,
                parking_memory->entries[1].information,
                parking_memory->entries[2].information,
                parking_memory->entries[3].information,
                parking_memory->entries[4].information);
        printf("Temp Sensor 1: %d        Temp Sensor 2: %d        Temp Sensor 3: %d        Temp Sensor 4: %d        Temp Sensor 5: %d\n",
                parking_memory->levels[0].temperature,
                parking_memory->levels[1].temperature,
                parking_memory->levels[2].temperature,
                parking_memory->levels[3].temperature,
                parking_memory->levels[4].temperature);
        printf("\n");
        printf("Entry 1 LPR: %s         Entry 1 GATE: %c        Exit 1 LPR: %s          Exit 1 Gate: %c\n",
                parking_memory->entries[0].plate,
                parking_memory->entries[0].state,
                parking_memory->exits[0].plate,
                parking_memory->exits[0].state);
        printf("Entry 2 LPR: %s         Entry 2 GATE: %c        Exit 2 LPR: %s          Exit 2 Gate: %c\n",
                parking_memory->entries[1].plate,
                parking_memory->entries[1].state,
                parking_memory->exits[1].plate,
                parking_memory->exits[1].state);
        printf("Entry 3 LPR: %s         Entry 3 GATE: %c        Exit 3 LPR: %s          Exit 3 Gate: %c\n",
                parking_memory->entries[2].plate,
                parking_memory->entries[2].state,
                parking_memory->exits[2].plate,
                parking_memory->exits[2].state);
        printf("Entry 4 LPR: %s         Entry 4 GATE: %c        Exit 4 LPR: %s          Exit 4 Gate: %c\n",
                parking_memory->entries[3].plate,
                parking_memory->entries[3].state,
                parking_memory->exits[3].plate,
                parking_memory->exits[3].state);
        printf("Entry 5 LPR: %s         Entry 5 GATE: %c        Exit 5 LPR: %s          Exit 5 Gate: %c\n",
                parking_memory->entries[4].plate,
                parking_memory->entries[4].state,
                parking_memory->exits[4].plate,
                parking_memory->exits[4].state);
        printf("\n");
        printf("Total billing revenue = $%.2f\n", billing);
        test++;
        usleep(display_refresh_time);
        system("clear");
    }
    return 0;
}

// Open the shared memory segment
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
    return shm;
}

int main()
{
    // Open the shared memory "PARKING" and save to variable shared memory
    struct car_park_shm *parking_memory = (struct car_park_shm *)Open_Shared_Memory("PARKING");

    // 2D array of strings
    char data[MAX_LINES][MAX_LEN];

    // Get plates from plates.txt
    FILE *file;
    file = fopen("plates.txt", "r");

    if (file == NULL){
        printf("Error opening file.\n");
        return 1;
    }

    // line will keep track of the number of lines read so far from the file
    int lines = 0;
    while (!feof(file) && !ferror(file)){
        if (fgets(data[lines], MAX_LEN, file) != NULL){           
            data[lines][6] = '\0';
            lines++;
        }
    }
    fclose(file);
    
    // Initialise plates hash table with lines(number of lines in plates.txt).
    size_t buckets = lines;
    if (!htab_init(&plate_hash_table, buckets)){
        printf("failed to initialise hash table\n");
        return EXIT_FAILURE;
    }

    // Add all values of the 2D array to the plates hash table
    for (int i = 0; i < lines; i++){
        htab_add(&plate_hash_table, data[i], i);
    }

    /*
    Create threads for use throughout the manager
    */
    /*#######################################################*/
    /*                 Entrance LPR Threads                  */
    /*#######################################################*/
    pthread_t Entry_LPR_Thread;
    for (int i = 0; i < (ENTRANCES); i++){
        if(pthread_create(&Entry_LPR_Thread, NULL, Entry_LPR_Status, &parking_memory->entries[i]) != 0){
            perror("Failed to create lpr_thread_entry");
        }
    }

    /*#######################################################*/
    /*              Entrance Boom Gate Threads               */
    /*#######################################################*/
     pthread_t Entry_BG_Thread;
     for (int i = 0; i < (ENTRANCES); i++){
        parking_memory->entries[i].state = 'C';
        if(pthread_create(&Entry_BG_Thread, NULL, operate_entry_gate, &parking_memory->entries[i]) != 0){
            perror("Failed to create entry_bg_thread");
        }
     }


    /*#######################################################*/
    /*              Entrance Info Sign Threads               */
    /*#######################################################*/
    pthread_t Entry_IS_Thread;
    for (int i = 0; i < (ENTRANCES); i++){
        if(pthread_create(&Entry_IS_Thread, NULL, Information_Sign_Display, &parking_memory->entries[i]) != 0){
            perror("Failed to create entry_IS_thread");
        }
    }

    /*#######################################################*/
    /*                    Exit LPR Threads                   */
    /*#######################################################*/
    pthread_t Exit_LPR_Thread;
    for (int i = 0; i < (EXITS); i++){
        if(pthread_create(&Exit_LPR_Thread, NULL, Exit_LPR_Status, &parking_memory->exits[i]) != 0){
            perror("Failed to create entry_bg_thread");
        }
    }

    /*#######################################################*/
    /*                Exit Boom Gate Threads                 */
    /*#######################################################*/
    pthread_t Exit_BG_Thread;
    for (int i = 0; i < (EXITS); i++){
        if(pthread_create(&Exit_BG_Thread, NULL, operate_exit_gate, &parking_memory->exits[i]) != 0){
            perror("Failed to create entry_bg_thread");
        }
    }

    /*#######################################################*/
    /*                   Level LPR Threads                   */
    /*#######################################################*/
    pthread_t Level_LPR_Thread;
    for (int i = 0; i < (LEVELS); i++){
        if(pthread_create(&Level_LPR_Thread, NULL, Level_LPR_Status, &parking_memory->levels[i]) != 0){
            perror("Failed to create level thread");
        }
    }

    /*#######################################################*/
    /*                      Status Thread                    */
    /*#######################################################*/
    pthread_t Status_Thread;
    pthread_create(&Status_Thread, NULL, Status_Display, parking_memory);

    /*#######################################################*/
    /*                     Join Threads                      */
    /*#######################################################*/
    for (int i = 0; i < ENTRANCES; i++){
        pthread_join(Entry_LPR_Thread, NULL);
        pthread_join(Entry_IS_Thread, NULL);
        pthread_join(Entry_BG_Thread, NULL);
    }

    for (int i = 0; i < EXITS; i++){
        pthread_join(Exit_LPR_Thread, NULL);
    }

    for (int i = 0; i < LEVELS; i++){
        pthread_join(Level_LPR_Thread, NULL);
    }

    pthread_join(Status_Thread, NULL);
    

    return 0;
}