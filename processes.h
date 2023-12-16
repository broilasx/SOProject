//
// Created by Marco Lucas on 30/03/2023.
//
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <fcntl.h>	// for O_ constants
#include <semaphore.h>
#include "string.h"
#include <pthread.h>
#include <time.h>
#include <stddef.h>
#include <ctype.h>


#ifndef PROJETOSO_PROCESSES_H
#define PROJETOSO_PROCESSES_H


#define MAX_LEN_MSG 100
#define SENSOR_PIPE  "sensor_pipe"
#define CONSOLE_PIPE "console_pipe"


void setup();
int QUEUE_SZ,N_WORKERS,MAX_KEYS,MAX_SENSORS,MAX_ALERTS;
int msqid;
FILE *system_config;
FILE *system_log;
char timestamp[10];
sem_t *mutex_log;

int write_pos, read_pos;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t empty;
sem_t full;


void error(char*message)
{
    printf("ERROR: %s\n",message);
    exit(1);
}


char* get_time()
{
    time_t seconds = time(NULL);;
    struct tm *timeStruct = localtime(&seconds);
    snprintf(timestamp,sizeof(timestamp), "%d:%d:%d", timeStruct->tm_hour, timeStruct->tm_min, timeStruct->tm_sec);
    //printf("%s\n",timestamp);
    return timestamp;
}

void logging(char *message)
{
    //system_log = fopen("/Users/marcolucas/sharedfoldervm/projetoSO/Log.txt", "a+");
    system_log = fopen("/home/kamiguru/Desktop/so/proj/Log.txt", "a+");
    fprintf(system_log,"%s %s\n",get_time(),message);
    fclose(system_log);
}

void wrong_command(char * buffer)
{
    printf("WRONG COMMAND\n");
    char message[MAX_LEN_MSG + 50];
    sem_wait(mutex_log);
    snprintf(message,MAX_LEN_MSG,"WRONG COMMAND => %s",buffer);
    logging(message);
    sem_post(mutex_log);
}

typedef struct InternalQueueBlock
{
    char id[33];
    char command[MAX_LEN_MSG];
}InternalQueueBlock;


typedef struct {
    InternalQueueBlock* listBlocks;
}InternalQueue;

typedef struct SharedData{
    pthread_cond_t alertCond;
    pthread_mutex_t alertMutex;
}SharedData;


void InternalQueueInitial(InternalQueue *queue)
{
    queue->listBlocks = (InternalQueueBlock *) malloc(sizeof(InternalQueueBlock)*QUEUE_SZ);
    sem_init(&empty, 0, QUEUE_SZ);
    sem_init(&full, 0, 0);
    write_pos = read_pos = 0;
}

void setup()
{
    int configurations[5];
    char line[10];
    //system_config = fopen("/Users/marcolucas/sharedfoldervm/projetoSO/Config.txt", "r");
    system_config = fopen("/home/kamiguru/Desktop/so/proj/Config.txt", "r");
    sem_unlink("MUTEX_LOG");
    mutex_log = sem_open("MUTEX_LOG",O_CREAT|O_EXCL,0700,1);

    sem_wait(mutex_log);
    //system_log = fopen("/Users/marcolucas/sharedfoldervm/projetoSO/Log.txt", "w");
    system_log = fopen("/home/kamiguru/Desktop/so/proj/Log.txt", "w");
    fprintf(system_log,"%s %s\n",get_time(),"HOME_IOT SIMULATOR STARTING");
    fclose(system_log);
    sem_post(mutex_log);

    int i = 0;
    while(fgets(line,10,system_config) != NULL)
    {
        //printf("%s",line);
        configurations[i] = atoi(line);
        i+=1;
    }
    if(configurations[0] < 1)
    {
        perror("QUEUE_SZ value should be >= 1!");
        fclose(system_config);
        exit(1);
    }
    else if(configurations[1] < 1)
    {
        perror("N_WORKERS value should be >= 1!");
        fclose(system_config);
        exit(1);
    }
    else if(configurations[2] < 1)
    {
        perror("MAX_KEYS value should be >= 1!");
        fclose(system_config);
        exit(1);
    }
    else if(configurations[3] < 1)
    {
        perror("MAX_SENSORS value should be >= 1!");
        fclose(system_config);
        exit(1);
    }
    else if(configurations[4] < 0)
    {
        perror("MAX_ALERTS value should be >= 0!");
        fclose(system_config);
        exit(1);
    }
    QUEUE_SZ = configurations[0];
    N_WORKERS = configurations[1];
    MAX_KEYS = configurations[2];
    MAX_SENSORS = configurations[3];
    MAX_ALERTS = configurations[4];
    fclose(system_config);
}




typedef struct SensorStats
{
    char key[33];
    int latest_value;
    int min_value;
    int max_value;
    int total;
    double avg_values;
    int counter_updates_key;
}SensorStats;

typedef struct Alerts
{
    char id[33];
    long id_console;
    char key[33];
    int status;
    int min_value;
    int max_value;
}Alerts;

typedef struct Sensors
{
    char id[33];
}Sensors;


typedef struct MessageStruct
{
    long mtype;
    char message[MAX_LEN_MSG];
}MessageStruct;



SensorStats* SEARCH_KEY(SensorStats *pointer, char* key)
{
    int counter = 0;
    while( ( pointer->key[0] == '\0'|| strcmp(pointer->key,key) != 0 ) && counter < MAX_KEYS)
    {
        if(pointer->key[0] == '\0')
        {
            pointer+=1;
            counter += 1;
            continue;
        }
        pointer+=1;
        counter += 1;
    }
    if(strcmp(pointer->key,key) == 0) return pointer;
    return NULL;
}

void LIST_STATS(SensorStats *pointer,int msqid,MessageStruct mq)
{
    int counter = 0;
    strcpy(mq.message,"KEY     LAST    MIN     MAX     AVG     COUNT\n");
    msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),0);
    while(counter < MAX_KEYS)
    {
        if(pointer->key[0] != '\0')
        {
            snprintf(mq.message,sizeof(mq.message),"%s      %d      %d      %d      %.2lf      %d\n",pointer->key,pointer->latest_value,pointer->min_value,pointer->max_value,pointer->avg_values,pointer->counter_updates_key);
            msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),0);
        }
        pointer+=1;
        counter+=1;
    }
}

void LIST_SENSORS(Sensors *pointer,int msqid,MessageStruct mq)
{
    int counter = 0;
    strcpy(mq.message,"ID\n");
    msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),0);
    while(counter < MAX_SENSORS)
    {
        if(pointer->id[0] != '\0')
        {
            snprintf(mq.message,sizeof(mq.message),"%s\n",pointer->id);
            msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),0);
        }
        pointer+=1;
        counter+=1;
    }
}

void LIST_ALERTS(Alerts *pointer,int msqid,MessageStruct mq)
{
    int counter = 0;
    strcpy(mq.message,"ID      KEY     MIN     MAX\n");
    msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),0);
    while(counter < MAX_ALERTS)
    {
        if(pointer->id[0] != '\0')
        {
            snprintf(mq.message,sizeof(mq.message),"%s      %s      %d      %d\n",pointer->id,pointer->key,pointer->min_value,
                     pointer->max_value);
            msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),0);
        }
        pointer+=1;
        counter+=1;
    }
}

void RESET_ARRAYSTATS(SensorStats *pointer)
{
    for(int i = 0; i < MAX_KEYS; i++)
    {
        pointer->key[0] = '\0';
        pointer->max_value = 0;
        pointer->min_value = 0;
        pointer->counter_updates_key = 0;
        pointer->total = 0;
        pointer->avg_values = 0;
        pointer += 1;
    }


}

void RESET_ARRAYALERTS(Alerts *pointer)
{
    for(int i = 0; i < MAX_ALERTS; i++)
    {
        pointer->id[0] = '\0';
        pointer->key[0] = '\0';
        pointer->max_value = 0;
        pointer->min_value = 0;
        pointer->status = 0;
        pointer += 1;
    }
}

void RESET_ARRAYSENSORS(Sensors *pointer)
{
    for(int i = 0; i < MAX_ALERTS; i++)
    {
        pointer->id[0] = '\0';
        pointer += 1;
    }
}

int REMOVE_ALERT(Alerts *pointer,char *id)
{
    for(int i = 0; i < MAX_ALERTS; i++)
    {
        if(strcmp(id,pointer->id) == 0)
        {
            pointer->id[0] = '\0';
            pointer->key[0] = '\0';
            pointer->max_value = 0;
            pointer->min_value = 0;
            pointer->status = 0;
            return 1;
        }
        pointer += 1;
    }
    return 0;

}
int UPDATE_STATS(SensorStats *pointer, char*key, int value)
{
    for(int i = 0; i < MAX_ALERTS; i++)
    {
        if(strcmp(key,pointer->key) == 0)
        {
            pointer->latest_value = value;
            if(value > pointer->max_value)
            {
                pointer->max_value = value;
            }
            if(value < pointer->max_value)
            {
                pointer->min_value = value;
            }
            pointer->counter_updates_key += 1;
            pointer->total += value;
            pointer->avg_values = pointer->total/pointer->counter_updates_key;
            return 1;
        }
        pointer += 1;
    }
    return 0;

}


int ADD_KEY(SensorStats *pointer, char*key, int value)
{
    for(int i = 0; i < MAX_KEYS; i++)
    {
        if(pointer->key[0] == '\0')
        {
            strcpy(pointer->key,key);
            pointer->latest_value = value;
            pointer->max_value = value;
            pointer->min_value = value;
            pointer->counter_updates_key = 0;
            pointer->total = value;
            pointer->avg_values = value;
            return 1;
        }
        pointer += 1;
    }
    return 0;
}

int ADD_SENSOR(Sensors *pointer, char*id)
{
    for(int i = 0; i < MAX_KEYS; i++)
    {
        if(pointer->id[0] == '\0')
        {
            strcpy(pointer->id,id);
            return 1;
        }
        pointer += 1;
    }
    return 0;
}


Sensors* SEARCH_SENSOR(Sensors *pointer,char *id)
{
    int counter = 0;
    while( ( pointer->id[0] == '\0' || strcmp(pointer->id,id) != 0 ) && counter < MAX_SENSORS)
    {
        if(pointer->id[0] == '\0')
        {
            pointer+=1;
            counter += 1;
            continue;
        }
        pointer+=1;
        counter += 1;
    }
    if(strcmp(pointer->id,id) == 0) return pointer;
    return NULL;
}

Alerts* SEARCH_ALERT(Alerts *pointer,char *id)
{
    int counter = 0;
    while( ( pointer->id[0] == '\0' || strcmp(pointer->id,id) != 0 ) && counter < MAX_ALERTS)
    {
        if(pointer->id[0] == '\0')
        {
            pointer+=1;
            counter += 1;
            continue;
        }
        pointer+=1;
        counter += 1;
    }
    if(strcmp(pointer->id,id) == 0) return pointer;
    return NULL;
}

Alerts* SEARCH_TRIGGERED_ALERT(Alerts *pointer)
{
    int counter = 0;
    while(pointer->status == 0 && counter < MAX_ALERTS)
    {
        pointer+=1;
        counter+=1;
    }
    if(pointer->status == 1)
    {
        printf("ALERT %s WAS TRIGGERED!\n",pointer->id);
        return pointer;
    }
    else return NULL;

}



int ADD_ALERT(Alerts *pointer,int msqid,MessageStruct mq,char *id, char*key, int min, int max)
{
    int counter = 0;
    if(SEARCH_ALERT(pointer,id) != NULL)
    {
        sem_wait(mutex_log);
        logging("ERROR: ALERT ID ALREADY EXISTS");
        sem_post(mutex_log);
        strcpy(mq.message,"ERROR: ALERT ID ALREADY EXISTS\n");
        msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),0);
    }
    else
    {
        while(pointer->id[0] != '\0' && counter < MAX_ALERTS)
        {
            pointer +=1;
            counter +=1;
        }
        if(pointer->id[0] == '\0')
        {
            strcpy(pointer->id,id);
            pointer->id_console = mq.mtype;
            strcpy(pointer->key,key);
            pointer->status = 0;
            pointer->min_value = min;
            pointer->max_value = max;
            return 1;
        }
    }
    return 0;
}

#endif //PROJETOSO_PROCESSES_H
