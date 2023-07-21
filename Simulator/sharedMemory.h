#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <semaphore.h>
#include <inttypes.h>
#include <pthread.h>
#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

/*#######################################################*/
/*              Structs for Shared Memory                */
/*#######################################################*/

struct entrance {
    pthread_mutex_t Entry_LPR_mutex;
    pthread_cond_t Entry_LPR_cond;
    char plate[6];
    char entry_lpr_padding[2];
    pthread_mutex_t Entry_BG_mutex;
    pthread_cond_t Entry_BG_cond;
    char state;
    char entry_bg_padding[7];
    pthread_mutex_t Entry_IS_mutex;
    pthread_cond_t Entry_IS_Cond;
    char information;
    char entry_sign_padding[7];
};

struct exit {
    pthread_mutex_t Exit_LPR_mutex;
    pthread_cond_t Exit_LPR_cond;
    char plate[6];
    char exit_lpr_padding[2];
    pthread_mutex_t Exit_BG_mutex;
    pthread_cond_t Exit_BG_cond;
    char state;
    char exit_bg_padding[7];
};

struct level {
    pthread_mutex_t Level_LPR_mutex;
    pthread_cond_t Level_LPR_cond;
    char plate[6];
    char level_lpr_padding[2];
    int16_t temperature;
    char alarm_state;
    char level_alarm_padding[5];
};

struct car_park_shm{
    struct entrance entries[5];
    struct exit exits[5];
    struct level levels[5];
};

#endif /* SHARED_MEMORY_H */