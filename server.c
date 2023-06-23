#include "segel.h"
#include "request.h"
#include "stdbool.h"
#include "stdio.h"
#include "stdlib.h"
#include "pthread.h"
// 
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// declare local functions.
void enqueue(int fd,struct timeval arrival_time);
void dequeue(RequestStats* currReq);
void* HandlerFunc(void* stats);

// HW3: Parse the new arguments too
void getargs(int *port,int* threads, int* queue_size, enum SchedulingAlgo* schedalg , int* max_size, int argc, char *argv[])
{
    if (argc < 5 || argc > 6) {
	fprintf(stderr, "Usage: %s <port>\n", argv[0]);
	exit(1);
    }
    *port = atoi(argv[1]);
    *threads = atoi(argv[2]);
    *queue_size = atoi(argv[3]);
    if(strcmp(atoi(argv[4]), "block") == 0) {*schedalg = BLOCK;}
    if(strcmp(atoi(argv[4]), "dt") == 0) {*schedalg = DT;}
    if(strcmp(atoi(argv[4]), "dh") == 0) {*schedalg = DH;}
    if(strcmp(atoi(argv[4]), "bf") == 0) {*schedalg = BF;}
    if(strcmp(atoi(argv[4]), "dynamic") == 0) {*schedalg = DYNAMIC;}
    if(strcmp(atoi(argv[4]), "random") == 0) {*schedalg = RANDOM;}
    else {
        fprintf(stderr,"Incorrect choice of algorithm. must be one of block/dt/dh/bf/dynamic or random.");
    }

    //sanity checks
    if(*threads <= 0 || *queue_size <= 0)
    {
        fprintf(stderr,"threads and queue_size must be a positive integer. \n");
        exit(1);
    }

    if(*schedalg == DYNAMIC) 
    {
        *max_size = atoi(argv[5]);
        if(*max_size <= 0)
        {
            fprintf(stderr,"max_size of queue must be a positive integer. \n");
            exit(1);
        }
        if(*max_size < *queue_size)
        {
            fprintf(stderr,"max size of queue must be atleast the current size of the queue. \n");
            exit(1);
        }
    }

}

//initialize global parameters for later use.
RequestStats* requests_array; // implementing a queue in c is gonna be tiring so im using an array instead.
int active_conn_ctr = 0;
int waiting_conn_ctr = 0;
pthread_mutex_t q_mutex;
pthread_cond_t q_empty,q_not_empty,q_not_full; // q_not_empty needed?

int main(int argc, char *argv[])
{
    int listenfd, connfd, port, clientlen, threads, queue_size,max_size;
    struct sockaddr_in clientaddr;
    enum SchedulingAlgo schedalg;

    getargs(&port, &threads, &queue_size, &schedalg, &max_size, argc, argv);

    requests_array = (RequestStats*)malloc(sizeof(RequestStats)*queue_size);

    pthread_mutex_init(&q_mutex,NULL);
    pthread_cond_init(&q_empty,NULL);
    pthread_cond_init(&q_not_empty,NULL); // #TODO: not full?
    pthread_cond_init(&q_not_full,NULL);

    // 
    // HW3: Create some threads...
    //
    pthread_t* myThreads = malloc(sizeof(pthread_t)*threads);
    ThreadStats* myThreadsStats = malloc(sizeof(ThreadStats)*threads);
    if(myThreads == NULL || myThreadsStats == NULL)
        exit(1);
    // create threads with the defined handler function of each thread.
    for (int i=0 ; i<threads ; i++){
        if(pthread_create(&myThreads[i],NULL,&HandlerFunc,(void*) &myThreadsStats[i]) != 0) // #TODO: implement handler_func.
        {
            perror("failed to create new thread :/ \n");
            exit(1);
        }
        myThreadsStats[i].thread_id = i;
        myThreadsStats[i].requests = 0;
        myThreadsStats[i].staticRequests = 0;
        myThreadsStats[i].dynamicRequests = 0;
    }


    listenfd = Open_listenfd(port);
    bool all_ears = false; // no busy-waiting for u.
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
        pthread_mutex_lock(&q_mutex); // wait for ur turn.
        struct timeval curr_time;
        gettimeofday(&curr_time,NULL);
        bool qHandlingNeeded = active_conn_ctr + waiting_conn_ctr >= queue_size; // used to determine if scheduling is needed.

        while(qHandlingNeeded)
        {
            if (schedalg == BLOCK)
            {
                while(qHandlingNeeded)
                {
                    pthread_cond_wait(&q_not_full,&q_mutex); // wait until queue isn't full.
                }
            }
            else if (schedalg == DT)
            {
                close(connfd); // drop new request.
                all_ears = true;
                break;
            }
            else if (schedalg == DH)
            {
                if(waiting_conn_ctr == 0) // no connection request is waiting so just drop the current.
                {
                    close(connfd);
                    all_ears = true;
                    break;
                }
                
                int to_drop = requests_array[0].fd; // oldest request is the first in array because im too lazy to implement a queue.
                close(to_drop);
                // fix the array.
                for(int i=0 ; i < (waiting_conn_ctr-1) ; i++)
                {
                    requests_array[i].fd = requests_array[i+1].fd;
                    requests_array[i].t_arrive = requests_array[i+1].t_arrive;
                    requests_array[i].t_dispatch = requests_array[i+1].t_dispatch;
                }
                waiting_conn_ctr--;
                pthread_cond_signal(&q_not_full); // signal when not full.
                continue; // needed?
            }

            else if(schedalg == BF)
            {
                pthread_cond_wait(&q_empty,&q_mutex);
                if(active_conn_ctr == 0)
                {
                    close(connfd);
                    all_ears = true;
                    break;
                }
            }
            else if(schedalg == DYNAMIC)
            {
                close(connfd); // drop new request.
                all_ears = true;
                if(queue_size + 1 <= max_size)
                {
                    queue_size++;
                }
                break;
            }

            else if(schedalg == RANDOM)
            {
                int toDrop_ctr = 0;
                if(waiting_conn_ctr == 0) // no waiting connections to drop.
                {
                    close(connfd);
                    all_ears = true;
                    break;
                }
                // drop half of the waiting connections.
                if(waiting_conn_ctr%2 == 0)
                    toDrop_ctr = waiting_conn_ctr/2;
                else
                    toDrop_ctr = waiting_conn_ctr/2 +1;
                int randomNum; // gonna be used to generate a random Num from 1 to the total num of waiting connections in array.
                while(toDrop_ctr > 0)
                {
                    randomNum = rand() % waiting_conn_ctr;
                    int toClose_fd = requests_array[randomNum].fd;
                    close(toClose_fd); // close chosen connection fd.

                    //now we are required to fix the array of waiting connections after removing a random waiting conn.
                    // to do so we "bubble" the array starting from the hole of the removed conn untill the end of the array.

                    for(int i = randomNum ; i < waiting_conn_ctr -1 ; i++)
                    {
                        requests_array[i].fd = requests_array[i+1].fd;
                        requests_array[i].t_arrive = requests_array[i+1].t_arrive;
                        requests_array[i].t_dispatch = requests_array[i+1].t_dispatch;
                    }
                    waiting_conn_ctr--;
                }

            }
        }

        if(all_ears)
        {
            pthread_mutex_unlock(&q_mutex);
            continue;
        }

        //#TODO: enqueue new connection.
        enqueue(connfd,curr_time); // enqueue new connection with current enqueuing time and fd.
        pthread_cond_signal(&q_not_empty);
        pthread_mutex_unlock(&q_mutex);

        //requestHandle(connfd);

        //Close(connfd);
    }
    // 
    // HW3: In general, don't handle the request in the main thread.
    // Save the relevant info in a buffer and have one of the worker threads 
    // do the work. 
    // }

    //add threads to set
    for(int i=0 ; i<threads ; ++i){
        if (pthread_join(myThreads[i], NULL) != 0) {
            perror("Failure in joining threads. \n");
            exit(1);
        }
    }

    //destroy mutex and its buddies.

    pthread_mutex_destroy(&q_mutex);
    pthread_cond_destroy(&q_empty);
    pthread_cond_destroy(&q_not_empty);
    pthread_cond_destroy(&q_not_full);

} 
// used for enqueuing coming connections in waiting requests array.
void enqueue(int fd,struct timeval arrival_time){
    requests_array[waiting_conn_ctr].fd = fd;
    requests_array[waiting_conn_ctr].t_arrive = arrival_time;
    waiting_conn_ctr++; // increment for next conn to be added;
}
// used to dispatch/dequeue existing waiting connection.
void dequeue(RequestStats* currReq){
    pthread_mutex_lock(&q_mutex); // Synchronize.
    while (waiting_conn_ctr == 0){
        pthread_cond_wait(&q_not_empty,&q_mutex); // wait untill there's atleast one waiting request to be handled.
    }
    // there is atleast 1 enqueued connection, dispatch the oldest one.
    *currReq = requests_array[0]; // current request to be dispatched is first requesy in array, aka the oldest one.
    struct timeval DispatchTime;
    gettimeofday(&DispatchTime,NULL); // get curr time of dispatch.
    currReq->t_dispatch = DispatchTime;
    // fix waiting connections array after oldest connection was dispatched.
    for (int i = 0; i < waiting_conn_ctr - 1; i++) {
        requests_array[i].fd=requests_array[i+1].fd;
        requests_array[i].t_arrive=requests_array[i+1].t_arrive;
        requests_array[i].t_dispatch=requests_array[i+1].t_dispatch;
    }
    waiting_conn_ctr--; // 1 waiting connection has been dispatched.
    active_conn_ctr ++; // dispatched connection is now an active connection.
    pthread_mutex_unlock(&q_mutex); // done. now you can unlock.
}

void* HandlerFunc(void* stats){
    RequestStats request;
    while(1){
        dequeue(&request); // Thread queues request.
        requestHandle(request.fd, request, (ThreadStats*) stats); // request handled
        Close(request.fd); // close request's fd.
        pthread_mutex_lock(&q_mutex); // lock to update counter properly.
        active_conn_ctr--;
        pthread_cond_signal(&q_not_full); // signal that queue isn't full.
        pthread_mutex_unlock(&q_mutex); // continue.
    }
}


 
