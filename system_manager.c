//
// Marco Lucas 2021219146
// Bruno Almeida 2021237081

// Sistemas Operativos
// Licenciatura em Engenharia Informática 2022/2023
//
#include "processes.h"


//SIGINT handler
void ctrlc_handler(int signal_num);

void create_shared_memory();

//threads
void *SENSOR_READER(void *id);
void *CONSOLE_READER(void *id);

//child processes
void WORKER(int id,int i,int*pipe);
void ALERTS_WATCHER(int id);

int shmid;
pid_t parent;


//array of unnamed pipes and worker status(so that dispatcher knows which worker is free)
int **worker_pipes;
int *worker_status;
pid_t *process_id;

//semaphores
sem_t *mutex_shm_sem;
sem_t *thread_mutex;

SharedData *shared_data;
pthread_mutexattr_t mutexAttr;
pthread_condattr_t condAttr;

//Initializing threads
long thread_id[3];
pthread_t threads[3];
int *destroy;

//shared memory region
void *region;
SensorStats *array1;
Alerts  *array2;
Sensors *array3;
int *worker_status;
int *alerts_counter;
int *keys_counter;
int *sensors_counter;
SharedData *shdata;


//internal queue
InternalQueue *queue;


void create_shared_memory()
{
    shmid = shmget(IPC_PRIVATE,sizeof(SharedData)+MAX_KEYS*sizeof(SensorStats)+MAX_ALERTS*sizeof(Alerts)+MAX_SENSORS*sizeof(Sensors)+N_WORKERS*sizeof(int)+4*sizeof(int),IPC_CREAT|0666);
    if(shmid == -1)
    {
        error("Error in creating shared memory");
        exit(1);
    }
    region = shmat(shmid, NULL, 0);
    if(region == (void *) -1)
    {
        perror("Error in allocating shared memory\n");
        exit(1);
    }

    shared_data = (SharedData*)region;
    array1 = (SensorStats*)(shared_data+1);
    array2 = (Alerts*)(array1 + MAX_KEYS);
    array3 = (Sensors*)(array2 + MAX_ALERTS);
    worker_status = (int*)(array3 + MAX_SENSORS);
    for(int i = 0; i < N_WORKERS; i++)
    {
        worker_status[i] = 0;
    }

    keys_counter = (int*)(worker_status + N_WORKERS); *keys_counter = 0;
    alerts_counter = (int*)(keys_counter + 1); *alerts_counter = 0;
    sensors_counter = (int*)(alerts_counter + 1); *sensors_counter = 0;


    RESET_ARRAYSTATS(array1);
    RESET_ARRAYALERTS(array2);
    RESET_ARRAYSENSORS(array3);

}

void other_signals_handler(int signal_num)
{
    char message[MAX_LEN_MSG];
    sem_wait(mutex_log);
    snprintf(message,sizeof(message),"SIGNAL %d RECEIVED",signal_num);
    logging(message);
    sem_post(mutex_log);
}


//system manager SIGINT
void ctrlc_handler(int signal_num)
{
    // Cleanup resources here...
    if( parent == getpid() )
    {
        sem_wait(mutex_log);
        logging("SIGNAL SIGINT RECEIVED");
        sem_post(mutex_log);
        //unlink named pipes
        unlink(SENSOR_PIPE);
        unlink(CONSOLE_PIPE);
        printf("1\n");
        //ending threads
        *destroy = 1;
        for(int i = 0; i < 3; i++)
        {
            sem_post(&empty);
            sem_post(&full);
            pthread_mutex_unlock(&mutex);
            pthread_join(threads[i],NULL);
        }
        printf("2\n");



       // if (sem_close(thread_mutex) != 0) { fprintf(stderr, "sem_close() failed. errno:%d\n", errno); }
        //if (sem_unlink("THREAD_MUTEX") != 0) { fprintf(stderr, "sem_unlink() failed. errno:%d\n", errno); }
        printf("3\n");
        sem_wait(mutex_log);
        logging("HOME_IOT SIMULATOR WAITING FOR LAST TASKS TO FINISH");
        sem_post(mutex_log);
        while(wait(NULL) > 0);
        free(destroy);


        for(int i = 0; i < N_WORKERS; i++)
        {
            free(worker_pipes[i]);
        }
        free(worker_pipes);
        free(process_id);
        printf("4\n");
        sem_wait(mutex_log);
        logging("WORKERS AND ALERTS WATCHER TERMINATED");
        sem_post(mutex_log);

        if(sem_close(mutex_shm_sem) != 0){fprintf(stderr, "sem_close() failed. errno:%d\n",errno);}
        if(sem_unlink("MUTEX_SHM_SEM") != 0){fprintf(stderr, "sem_unlink() failed. errno:%d\n",errno);}

        /*
        for(int i = 1; i < N_WORKERS+1; i++)
        {
            printf("WORKER %d STATUS %d\n",i,worker_status[i-1]);
        }
        */

        //terminate shared memory and message queue
        printf("5\n");
        shmdt(region);
        shmctl(shmid, IPC_RMID, NULL);
        msgctl(msqid, IPC_RMID, NULL);


        sem_destroy(&empty);
        sem_destroy(&full);
        pthread_mutex_destroy(&mutex);
        //closing log semaphore
        sem_wait(mutex_log);
        logging("HOME_IOT SIMULATOR CLOSING");
        sem_post(mutex_log);

        if(sem_close(mutex_log) != 0){fprintf(stderr, "sem_close() failed. errno:%d\n",errno);}
        if(sem_unlink("MUTEX_LOG") != 0){fprintf(stderr, "sem_unlink() failed. errno:%d\n",errno);}

        exit(0);
    }
    else
    {
        for(int i = 0; i < N_WORKERS+1; i++)
        {
            if(process_id[i] == getpid())
            {
                if(i != 0)
                {
                    close(worker_pipes[i-1][0]);
                    close(worker_pipes[i-1][1]);
                    free(worker_pipes[i-1]);
                    printf("Ending worker %d\n",i);
                    exit(0);
                }
                else
                {
                    printf("ENDING ALERTS WATCHER\n");
                    exit(0);
                }
            }
        }

    }

}



//DISPATCHER THREAD
void *DISPATCHER(void *id)
{
    printf("Starting DISPATCHER\n");
    int i = 1;
    int wrong = 0;
    char msg[MAX_LEN_MSG];
    char log[MAX_LEN_MSG+MAX_LEN_MSG];
    while(*destroy == 0)
    {
        sem_wait(&full);
        pthread_mutex_lock(&mutex);

        strcpy(msg,queue->listBlocks[read_pos].command);

        if(msg[0] == '\0' || strcmp(msg,"") == 0)
        {
            read_pos = (read_pos+1) % QUEUE_SZ;
            pthread_mutex_unlock(&mutex);
            sem_post(&empty);
            continue;
        }

        //check if message is alphanumeric
        for(int j = 0; j < MAX_LEN_MSG; j++)
        {
            if(msg[j] == '\0')
            {
                break;
            }
            else if(msg[j] == '#' || msg[j] == ' ' || msg[j] == '_') continue;
            else if(isalnum(msg[j]) == 0)
            {
                wrong = 1;
                break;
            }
        }

        if(wrong == 1)
        {
            wrong_command(msg);
            read_pos = (read_pos+1) % QUEUE_SZ;
            pthread_mutex_unlock(&mutex);
            sem_post(&empty);
            continue;
        }


        if(worker_status[i-1] == 0)
        {
            sem_wait(mutex_log);
            snprintf(log,sizeof(log),"DISPATCHER: %s SENT FOR PROCESSING IN WORKER%d",msg,i);
            logging(log);
            sem_post(mutex_log);
            close(worker_pipes[i-1][0]);
            if (write(worker_pipes[i-1][1],&queue->listBlocks[read_pos], sizeof(InternalQueueBlock)) < 0)
            {
                perror("Error in writing to worker pipe");
            }
        }
        i+=1;
        if(i > N_WORKERS) i = 1;
        strcpy(queue->listBlocks[read_pos].command,"");

        read_pos = (read_pos+1) % QUEUE_SZ;

        pthread_mutex_unlock(&mutex);
        sem_post(&empty);
    }
    sem_wait(thread_mutex);
    printf("Ending DISPATCHER\n");
    sem_post(thread_mutex);
    pthread_exit(NULL);
}

void *SENSOR_READER(void *id)
{
    printf("Starting SENSOR_READER\n");
    int fd,nread;
    if ( ( fd = open(SENSOR_PIPE, O_NONBLOCK) ) < 0)
    {
        perror("Cannot open sensor pipe for reading: ");
        pthread_exit(NULL);
    }
    InternalQueueBlock block;

    while(*destroy == 0)
    {
        nread = read(fd, &block, sizeof(block));
        if(nread > 0)
        {
            sem_wait(&empty);
            pthread_mutex_lock(&mutex);

            queue->listBlocks[write_pos] = block;
            write_pos = (write_pos+1) % QUEUE_SZ;

            sem_post(&full);
            pthread_mutex_unlock(&mutex);
        }
    }
    sem_wait(thread_mutex);
    printf("Ending SENSOR_READER\n");
    sem_post(thread_mutex);
    pthread_exit(NULL);
}

void *CONSOLE_READER(void *id)
{
    printf("Starting CONSOLE_READER\n");
    int fd,nread;
    if ( ( fd = open(CONSOLE_PIPE, O_NONBLOCK) ) < 0)
    {
        perror("Cannot open console pipe for reading: ");
        pthread_exit(NULL);
    }
    InternalQueueBlock block;

    while(*destroy == 0)
    {
        nread = read(fd, &block, sizeof(block));
        if(nread > 0)
        {
            sem_wait(&empty);
            pthread_mutex_lock(&mutex);

            queue->listBlocks[write_pos] = block;
            write_pos = (write_pos+1) % QUEUE_SZ;

            sem_post(&full);
            pthread_mutex_unlock(&mutex);
        }
    }
    sem_wait(thread_mutex);
    printf("Ending CONSOLE_READER\n");
    sem_post(thread_mutex);
    pthread_exit(NULL);

}

int CHECK_ALERT(Alerts *pointer,char *key,int value)
{
    int counter = 0 ;
    while(counter < MAX_ALERTS)
    {
        if(strcmp(pointer->key,key) == 0)
        {
            if(value < pointer->min_value || value > pointer->max_value)
            {
                pointer->status = 1;
                return 1;
            }
        }
        counter+=1;
        pointer+=1;
    }
    return 0;
}

void WORKER(int id,int i,int*pipe)
{
    InternalQueueBlock block;
    char buffer[MAX_LEN_MSG];
    char copy[MAX_LEN_MSG];
    char copy2[MAX_LEN_MSG];
    char temp[MAX_LEN_MSG];
    char temp2[MAX_LEN_MSG];
    char message[MAX_LEN_MSG];

    MessageStruct mq;
    msqid = msgget(1234,0);
    char *ptr,*ptr2,*ptr3;
    close(pipe[1]);

    while(1)
    {
        read(pipe[0], &block, sizeof(block));

        worker_status[i-1] = 1;
        sem_wait(mutex_shm_sem);

        strcpy(buffer,block.command);
        strcpy(temp,buffer);
        strcpy(temp2,buffer);
        strcpy(copy,buffer);
        strcpy(copy2,buffer);
        strcpy(message,buffer);

        char *delim1 = "\0";
        char *delim2 = " ";
        char *delim3 = "#";

        ptr = strtok(temp2,delim1);
        ptr2 = strtok(temp,delim2);
        ptr3 = strtok(buffer,delim3);


        if(ptr != NULL)
        {
            mq.mtype = atoi(block.id);
            //dealing with user console commands
            if(strcmp(ptr,"stats") == 0)
            {
                LIST_STATS(array1,msqid,mq);
                sem_wait(mutex_log);
                snprintf(message,sizeof(message),"WORKER%d: LISTING STATS PROCESSING COMPLETED",i);
                logging(message);
                sem_post(mutex_log);
                worker_status[i-1] = 0;
                sem_post(mutex_shm_sem);
                continue;
            }
            else if(strcmp(ptr,"reset") == 0)
            {
                RESET_ARRAYSTATS(array1);
                RESET_ARRAYSENSORS(array3);
                *sensors_counter = 0;
                *keys_counter = 0;

                strcpy(mq.message,"OK\n");
                msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),0);

                sem_wait(mutex_log);
                snprintf(message,sizeof(message),"WORKER%d: RESETTING STATS PROCESSING COMPLETED",i);
                logging(message);
                sem_post(mutex_log);

                worker_status[i-1] = 0;
                sem_post(mutex_shm_sem);
                continue;
            }
            else if( strcmp(ptr,"list_alerts") == 0)
            {
                LIST_ALERTS(array2,msqid,mq);

                sem_wait(mutex_log);
                snprintf(message,sizeof(message),"WORKER%d: LISTING ALERTS PROCESSING COMPLETED",i);
                logging(message);
                sem_post(mutex_log);
                worker_status[i-1] = 0;
                sem_post(mutex_shm_sem);
                continue;
            }
            else if(strcmp(ptr,"sensors") == 0)
            {
                LIST_SENSORS(array3,msqid,mq);
                sem_wait(mutex_log);
                snprintf(message,sizeof(message),"WORKER%d: LISTING SENSORS PROCESSING COMPLETED",i);
                logging(message);
                worker_status[i-1] = 0;
                sem_post(mutex_shm_sem);
                continue;
            }
        }

        if(ptr2 != NULL)
        {
            mq.mtype = atoi(block.id);
            ptr2 = strtok(copy, delim2);
            if(strcmp(ptr2,"add_alert") == 0 )
            {
                char id[33],key[33];
                int min,max;

                ptr2 = strtok(NULL, delim2);
                if(ptr2 == NULL)
                {
                    wrong_command(message);
                    worker_status[i-1] = 0;
                    sem_post(mutex_shm_sem);
                    continue;
                }
                strcpy(id,ptr2);


                ptr2 = strtok(NULL, delim2);
                if(ptr2 == NULL)
                {
                    wrong_command(message);
                    worker_status[i-1] = 0;
                    sem_post(mutex_shm_sem);
                    continue;
                }
                strcpy(key,ptr2);

                ptr2 = strtok(NULL, delim2);
                if(ptr2 == NULL)
                {
                    wrong_command(message);
                    worker_status[i-1] = 0;
                    sem_post(mutex_shm_sem);
                    continue;
                }
                min = atoi(ptr2);

                ptr2 = strtok(NULL, delim1);
                if(ptr2 == NULL)
                {
                    wrong_command(message);
                    worker_status[i-1] = 0;
                    sem_post(mutex_shm_sem);
                    continue;
                }
                max = atoi(ptr2);
                if(min == 0 || max == 0)
                {
                    wrong_command(message);
                    worker_status[i-1] = 0;
                    sem_post(mutex_shm_sem);
                    continue;
                }
                if(*alerts_counter == MAX_ALERTS)
                {
                    sem_wait(mutex_log);
                    logging("!LIMIT OF ALERTS REACHED!");
                    sem_post(mutex_log);
                    strcpy(mq.message,"!LIMIT OF ALERTS REACHED!\n");
                    msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),0);
                    worker_status[i-1] = 0;
                    sem_post(mutex_shm_sem);
                    continue;
                }
                ADD_ALERT(array2,msqid,mq,id,key,min,max);

                strcpy(mq.message,"OK\n");
                msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),0);

                *alerts_counter += 1;
                sem_wait(mutex_log);
                snprintf(message,sizeof(message),"WORKER%d: ADD ALERT %s PROCESSING COMPLETED",i,id);
                logging(message);
                sem_post(mutex_log);
                worker_status[i-1] = 0;
                sem_post(mutex_shm_sem);
                continue;
            }
            else if(strcmp(ptr2,"remove_alert") == 0)
            {
                char id[33];
                ptr2 = strtok(NULL, delim2);
                if(ptr2 == NULL)
                {
                    wrong_command(message);
                    worker_status[i-1] = 0;
                    sem_post(mutex_shm_sem);
                    continue;
                }
                strcpy(id,ptr2);
                REMOVE_ALERT(array2,id);
                *alerts_counter -= 1;
                sem_wait(mutex_log);
                snprintf(message,sizeof(message),"WORKER%d: REMOVE ALERT %s PROCESSING COMPLETED",i,id);
                logging(message);
                sem_post(mutex_log);
                strcpy(mq.message,"OK\n");
                msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),0);
                worker_status[i-1] = 0;
                sem_post(mutex_shm_sem);
                continue;
            }
        }

        if(ptr3 != NULL)    //dealing with sensor data
        {
            char id[33];
            char key[33];
            int value;

            ptr3 = strtok(copy2,delim3);
            if(ptr3 == NULL)
            {
                wrong_command(message);
                worker_status[i-1] = 0;
                sem_post(mutex_shm_sem);
                continue;
            }
            strcpy(id,ptr3);


            ptr3 = strtok(NULL, delim3);
            if(ptr3 == NULL)
            {
                wrong_command(message);
                worker_status[i-1] = 0;
                sem_post(mutex_shm_sem);
                continue;
            }
            strcpy(key,ptr3);

            ptr3 = strtok(NULL, delim1);
            if(ptr3 == NULL)
            {
                wrong_command(message);
                worker_status[i-1] = 0;
                sem_post(mutex_shm_sem);
                continue;
            }
            value = atoi(ptr3);

            if(SEARCH_SENSOR(array3,id) == NULL)
            {
                if(ADD_SENSOR(array3,buffer) == 1) printf("SENSOR ADDED\n");
                else
                {
                    sem_wait(mutex_log);
                    logging("!LIMIT OF SENSORS REACHED!");
                    sem_post(mutex_log);


                    worker_status[i-1] = 0;
                    sem_post(mutex_shm_sem);
                    continue;
                }
            }

            //see if it triggers an alert

            if(CHECK_ALERT(array2,key,value) == 1)
            {
                pthread_mutex_lock(&shared_data->alertMutex);
                pthread_cond_signal(&shared_data->alertCond);
                pthread_mutex_unlock(&shared_data->alertMutex);
                worker_status[i-1] = 0;
                sem_post(mutex_shm_sem);
                continue;

            }


            int result = UPDATE_STATS(array1, key, value);

            if(result == 1)
            {
                sem_wait(mutex_log);
                snprintf(message,MAX_LEN_MSG,"WORKER%d: %s DATA PROCESSING COMPLETED",i,key);
                logging(message);
                sem_post(mutex_log);
                worker_status[i-1] = 0;
                sem_post(mutex_shm_sem);
                continue;
            }

            if(result == 0) //key is previously not in keys array
            {
                if(*keys_counter == MAX_KEYS)
                {
                    sem_wait(mutex_log);
                    logging("!LIMIT OF KEYS REACHED!");
                    sem_post(mutex_log);
                    strcpy(mq.message,"!LIMIT OF KEYS REACHED!\n");
                    msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),0);
                    worker_status[i-1] = 0;
                    sem_post(mutex_shm_sem);
                    continue;
                }
                else
                {
                    ADD_KEY(array1, key, value);
                    printf("KEY ADDED\n");

                    sem_wait(mutex_log);
                    snprintf(message,MAX_LEN_MSG,"WORKER%d: %s DATA KEY INSERTION COMPLETED",i,key);
                    logging(message);
                    sem_post(mutex_log);
                    *keys_counter += 1;
                    worker_status[i-1] = 0;
                    sem_post(mutex_shm_sem);
                    continue;
                }
            }

        }
        else
        {

            wrong_command(message);
            worker_status[i-1] = 0;
            sem_post(mutex_shm_sem);
            continue;
        }
    }
}







void ALERTS_WATCHER(int id)
{
    MessageStruct mq;
    int msqid = msgget(1234,0);
    Alerts *match;
    while(1)
    {
        pthread_mutex_lock(&shared_data->alertMutex);
        while( (match = SEARCH_TRIGGERED_ALERT(array2)) == NULL)
        {
            pthread_mutex_unlock(&shared_data->alertMutex);
            pthread_cond_wait(&shared_data->alertCond,&shared_data->alertMutex);
        }
        mq.mtype = match->id_console;
        snprintf(mq.message,sizeof(mq.message),"ALERT %s (%s %d TO %d) TRIGGERED",match->id,match->key,match->min_value,match->max_value);
        sem_wait(mutex_log);
        logging(mq.message);
        sem_post(mutex_log);
        if(match != NULL)
        {
            msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),IPC_NOWAIT);
            match->status = 0;
        }
        pthread_mutex_unlock(&shared_data->alertMutex);

    }
}




int main() {
    //setting up configurations
    setup();
    parent = getpid();
    signal(SIGINT, ctrlc_handler);
    signal(SIGKILL,other_signals_handler);
    signal(SIGTERM, other_signals_handler);
    signal(SIGTSTP,other_signals_handler);

    msqid = msgget(1234, IPC_CREAT | 0777);

    //creating shared memory
    create_shared_memory();

    //setting up mutex and conditional variable to be used to signal between alerts watcher and workers

    pthread_mutexattr_init(&mutexAttr);
    pthread_mutexattr_setpshared(&mutexAttr, PTHREAD_PROCESS_SHARED);

    pthread_condattr_init(&condAttr);
    pthread_condattr_setpshared(&condAttr, PTHREAD_PROCESS_SHARED);

    pthread_cond_init(&shared_data->alertCond, &condAttr);
    pthread_mutex_init(&shared_data->alertMutex, &mutexAttr);



    //creating semaphore
    sem_unlink("MUTEX_SHM_SEM");
    mutex_shm_sem = sem_open("MUTEX_SHM_SEM", O_CREAT | O_EXCL, 0700, 1);

    int total_childs = N_WORKERS + 1;

    worker_pipes = (int **) malloc(N_WORKERS * sizeof(int *));
    process_id = (int *) malloc(total_childs * sizeof(int));

    int i = 0;
    while (i < N_WORKERS)
    {
        worker_pipes[i] = malloc(sizeof(int) * 2);
        if (worker_pipes[i] == NULL)
        {
            printf("ERROR IN MALLOC FOR PIPE FOR WORKER %d", i + 1);
            exit(1);
        }
        if (pipe(worker_pipes[i]) == -1) {
            printf("ERROR IN CREATING PIPE FOR WORKER %d", i + 1);
            exit(1);
        }
        i += 1;
    }




    //creating workers and alerts watcher
    //as well as estabilishing unnamed pipes
    // between workers and system manager
    char message[MAX_LEN_MSG];
    pid_t pid;
    for (int i = 0; i < N_WORKERS + 1; i++)
    {
        pid = fork();
        if (pid == 0)
        {
            process_id[i] = getpid();
            if (i == 0)
            {
                sem_wait(mutex_log);
                logging("PROCESS ALERTS_WATCHER CREATED");
                printf("Starting ALERTS_WATCHER\n");
                sem_post(mutex_log);
                ALERTS_WATCHER(i);
            }
            else
            {
                sem_wait(mutex_log);
                snprintf(message, MAX_LEN_MSG, "WORKER %d READY", i);
                printf("Starting WORKER %d\n",i);
                logging(message);
                sem_post(mutex_log);
                WORKER(pid, i, worker_pipes[i - 1]);
            }
        }

    }

    //creating named pipes
    if ((mkfifo(SENSOR_PIPE, O_CREAT | O_EXCL | 0600) < 0) && (errno != EEXIST)) {
        perror("Cannot create SENSOR_PIPE: ");

    }
    if ((mkfifo(CONSOLE_PIPE, O_CREAT | O_EXCL | 0600) < 0) && (errno != EEXIST)) {
        perror("Cannot create CONSOLE_PIPE: ");

    }
    destroy = (int *) malloc(sizeof(int));
    *destroy = 0;


    sem_unlink("THREAD_MUTEX");
    thread_mutex = sem_open("THREAD_MUTEX", O_CREAT | O_EXCL, 0700, 1);

    //creating internal queue
    queue = (InternalQueue *) malloc(sizeof(InternalQueue));
    InternalQueueInitial(queue);

    //creating dispatcher
    thread_id[0] = 0;
    pthread_create(&threads[0], NULL, DISPATCHER, &thread_id[0]);

    sem_wait(mutex_log);
    logging("THREAD DISPATCHER CREATED");
    sem_post(mutex_log);

    //creating sensor reader
    thread_id[1] = 1;
    pthread_create(&threads[1], NULL, SENSOR_READER, &thread_id[1]);

    sem_wait(mutex_log);
    logging("THREAD SENSOR_READER CREATED");
    sem_post(mutex_log);

    //creating console reader
    thread_id[2] = 2;
    pthread_create(&threads[2], NULL, CONSOLE_READER, &thread_id[2]);

    sem_wait(mutex_log);
    logging("THREAD CONSOLE_READER CREATED");
    sem_post(mutex_log);

    /*
    sleep(2);
    pthread_mutex_lock(&shared_data->alertMutex);
    pthread_cond_signal(&shared_data->alertCond);
    pthread_mutex_unlock(&shared_data->alertMutex);
    printf("SIGNAL SENT\n");
    */



    //msqid = msgget(1234, IPC_CREAT|0777);
    /*
    MessageStruct mq;
    mq.mtype = 1;

    strcpy(mq.message,"OLA, EU SOU O MARCO\n");
    printf("worker started\n");
    msgsnd(msqid,&mq,sizeof(MessageStruct)-sizeof(long),0);
    */

    pause();

}