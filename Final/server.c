#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <errno.h>
#include <error.h>
#include <semaphore.h>

#define NUM_OF_NEIGHBORS 255
#define MAX 1024

int fd, fd2, size=0, full=0, workers=0, numOfThreads = 0, cacheSize=0, threadIndex=0, maxThreads, ctrlc=0, socket2,written=0;
int pass=0, readers=0, writers=0, cacheIndex=0, totalNeighbors=0, finishedThreads=0, ARGS=0, capacitySignal=0;
int mainThreadWaiting=0, clientSocket = 0, instance, priority=2, prio=0;
int *visited;
float rate;
char logMsg[MAX];
char *logFile = NULL;

pthread_mutex_t mutex;
pthread_mutex_t mainThread;
pthread_mutex_t priorityMutex;
pthread_mutex_t capacitySignalMutex;
pthread_mutex_t cacheMutex;
pthread_cond_t mainCond= PTHREAD_COND_INITIALIZER;
pthread_cond_t priorityCond= PTHREAD_COND_INITIALIZER;
pthread_cond_t capacitySignalCond= PTHREAD_COND_INITIALIZER;

pthread_cond_t *conditions;
pthread_t *threads;
pthread_t capacityThread;
pthread_mutex_t *assignment;

typedef struct vertex{
    int id;
    int index;
    int children[NUM_OF_NEIGHBORS];
}vertex;

typedef struct cacheDS{
    int source;
    int destination;
    int index;
    int path[MAX];
}cacheDS;

typedef struct type{
    pid_t pid;
    int source;
    int destination;
} type;

typedef struct threadType {
    int threadID;
    pid_t clientPID;
    int busy;
    int clientSocket;
    type job;
} threadType;

threadType *threadInfo;
cacheDS *cache;
vertex *vertices;
type *types;

int check(int id);
int BFS(int source, int destination, int ID);
void catcher(int signum);
vertex getVertex(int source);
int isVisited(int num);
void clean();
void unlocking();

int timeStamp() {
    int count=0;
    char msg[MAX];
    time_t date=time(NULL);

    char* time = (char*) malloc(sizeof(char)*strlen(asctime(localtime(&date))));
    strncpy(time, asctime(localtime(&date)),strlen(asctime(localtime(&date))));
    int strSize = strlen(time)-1;
    time[strSize]='\0';
    
    sprintf(msg, "%s : ",time);

    for (int i = 0; msg[i]!=0; ++i) {
        count++;
    }

    write(fd2, msg, count*sizeof(char));

    memset(msg, 0, sizeof(char)*MAX);

    free(time);
    return 0;
}


void* threadFunc(void* arg){
    threadType* t = (threadType*) arg;
    char message[MAX];
    int count=0;

    while(ctrlc==0) {
        if(pthread_mutex_lock(&mutex)!=0){
            perror("Error pthread_mutex_lock smutex\n");
            clean();
        }

        sprintf(logMsg, "Thread #%d: waiting for connection\n",t->threadID);

        for (int i = 0; logMsg[i]!=0; ++i) {
            count++;
        }
        
        timeStamp();
        write(fd2, logMsg, count* sizeof(char));
        memset(logMsg, 0, sizeof(char)*MAX);
        if(pthread_mutex_unlock(&mutex)!=0){
            perror("Error pthread_mutex_unlock mutex\n");
            clean();
        }

        if(capacitySignal){

            if(pthread_mutex_lock(&capacitySignalMutex)!=0){
                perror("Error pthread_mutex_lock mainThread\n");
                clean();
            }

            if(pthread_cond_signal(&capacitySignalCond) !=0){
                perror("Error pthread_cond_signal mainCond\n");
                clean();
            }
            
            if(pthread_mutex_unlock(&capacitySignalMutex)!=0){
                perror("Error pthread_mutex_unlock mainThread\n");
                clean();
            }
        }
        

        if(pthread_mutex_lock(&assignment[t->threadID])!=0){
            perror("Error pthread_mutex_lock assignment[t->threadID] (threadFunc)");
            clean();
        }

        if(pthread_cond_wait(&conditions[t->threadID], &assignment[t->threadID])!=0){
            perror("Error pthread_cond_wait conditions[t->threadID]\n");
            clean();
        }

        if(ctrlc==1){ 
            if(pthread_mutex_unlock(&assignment[t->threadID])!=0){
                perror("Error pthread_mutex_unlock &assignment[t->threadID\n");
                clean();
            }
            break;
        }
        if(t->busy==0){

            if(pthread_mutex_unlock(&assignment[t->threadID])!=0){
                perror("Error pthread_mutex_unlock assignment[t->threadID\n");
            }
            continue;
        }
        if(t->busy==1) {
            workers++;
            if(pthread_mutex_lock(&mutex)!=0){
                perror("Error pthread_mutex_lock mutex\n");
                clean();
            }
            memset(logMsg, 0, sizeof(char)*MAX);
            count = 0;

            sprintf(logMsg, "Thread #%d: searching database for a path from node %d to node %d\n", t->threadID,
                t->job.source,t->job.destination);

            for (int i = 0; logMsg[i]!=0; ++i) {
                count++;
            }
            
            timeStamp();
            write(fd2, logMsg, count* sizeof(char));
            memset(logMsg, 0, sizeof(char)*MAX);
            if(pthread_mutex_unlock(&mutex)!=0){
                perror("Error pthread_mutex_unlock\n");
                clean();
            }

            int m=BFS(t->job.source, t->job.destination, t->threadID);      

            if(m>-1){

                if(pthread_mutex_lock(&mutex)!=0){
                    perror("Error pthread_mutex_lock mutex\n");
                    clean();
                }
                count = 0;
                sprintf(logMsg, "Thread #%d: path calculated: ", t->threadID);

                memset(message, 0, sizeof(char)*MAX);
                char* pos=message;

                for (int i = 0; i < cache[m].index; ++i){
                    pos+=sprintf(pos, "%d->",cache[m].path[i]);
                }

                int msgsize = strlen(message);

                message[msgsize-2]='\0';

                if(write(t->clientSocket, message , sizeof(cache[m].path)) < 0)
                    printf("error while sending message\n");
                                
                for (int i = 0; logMsg[i]!=0; ++i) {
                    count++;
                }
                strcat(message,"\n");
                int count2=0;
                for (int i = 0; message[i]!=0; ++i) {
                    count2++;
                }

                timeStamp();
                write(fd2, logMsg, count*sizeof(char));
                write(fd2, message, count2*sizeof(char));

                memset(logMsg, 0, sizeof(char)*MAX);

                timeStamp();
                sprintf(logMsg, "Thread #%d: responding to client and adding path to database.\n", t->threadID);
                
                count=0;
                for (int i = 0; logMsg[i]!=0; ++i) {
                    count++;
                }

                timeStamp();
                write(fd2, logMsg, count* sizeof(char));
                memset(logMsg, 0, sizeof(char)*MAX);
                if(pthread_mutex_unlock(&mutex)!=0){
                    perror("Error pthread_mutex_unlock\n");
                    clean();
                }
            }
            else if(m==-1){
                sprintf(message, "NO PATH");

                if(write(t->clientSocket , message , strlen(message)) < 0)
                    printf("error while sending message\n");

                if(pthread_mutex_lock(&mutex)!=0){
                    perror("Error pthread_mutex_lock mutex\n");
                    clean();
                }

                count = 0;
                memset(logMsg, 0, sizeof(char)*MAX);
                sprintf(logMsg, "Thread #%d: path not possible from node %d to %d\n",t->threadID, t->job.source,t->job.destination);

                for (int i = 0; logMsg[i]!=0; ++i) {
                    count++;
                }
                
                timeStamp();
                write(fd2, logMsg, count* sizeof(char));
                memset(logMsg, 0, sizeof(char)*MAX);

                sprintf(logMsg, "Thread #%d: responding to client and adding path to database.\n", t->threadID);
                count=0;
                for (int i = 0; logMsg[i]!=0; ++i) {
                    count++;
                }
                
                timeStamp();
                write(fd2, logMsg, count* sizeof(char));

                if(pthread_mutex_unlock(&mutex)!=0){
                    perror("Error pthread_mutex_unlock\n");
                    clean();
                }
            }
            memset(message, 0, sizeof(message));
        }
        t->busy=0;
        workers--;

        if(pthread_mutex_unlock(&assignment[t->threadID])!=0){
            perror("Error pthread_mutex_unlock assignment[t->threadID]\n");
            clean();
        }

        if(mainThreadWaiting==1){

            if(pthread_mutex_lock(&mainThread)!=0){
                perror("Error pthread_mutex_lock mainThread\n");
                clean();
            }


            if(pthread_cond_signal(&mainCond) !=0){
                perror("Error pthread_cond_signal mainCond\n");
                clean();
            }


            if(pthread_mutex_unlock(&mainThread)!=0){
                perror("Error pthread_mutex_unlock mainThread\n");
                clean();
            }
        }
    }
    finishedThreads++;

    void* returnValue=NULL;
    return returnValue;
}

void* capacityChecker(){
    int limitwritten = 0, count=0, increment=5;

    while (ctrlc==0) {
        rate = ((float)workers/(float)numOfThreads);

        if (rate > 0.75) {
            if(limitwritten == 0){

                if(pthread_mutex_lock(&mutex)!=0){
                    perror("Error pthread_mutex_lock\n");
                    clean();
                }

                count = 0;
                memset(logMsg, 0, sizeof(char)*MAX);
                sprintf(logMsg, "System load %.3f%%, pool extended to %d threads\n",
                        rate, increment+numOfThreads);

                for (int i = 0; logMsg[i]!=0; ++i) {
                    count++;
                }
                timeStamp();
                write(fd2, logMsg, count* sizeof(char));
                memset(logMsg, 0, sizeof(char)*MAX);

                if(pthread_mutex_unlock(&mutex)!=0){
                    perror("Error pthread_mutex_unlock\n");
                    clean();
                }

                limitwritten=1;
            }
            if(ctrlc)
                break;

            int d = (int)numOfThreads/4;

            if(maxThreads <= numOfThreads+d){
                if(written==0) {

                    if(pthread_mutex_lock(&mutex)!=0){
                        perror("Error pthread_mutex_lock mutex\n");
                        clean();
                    }
                    int count = 0;

                    sprintf(logMsg, "Cannot create more threads. Existing thread number hit the maximum value.\n");

                    for (int i = 0; logMsg[i]!=0; ++i) {
                        count++;
                    }

                    timeStamp();
                    write(fd2, logMsg, count* sizeof(char));
                    memset(logMsg, 0, sizeof(char)*MAX);

                    count = 0;
                    if(pthread_mutex_unlock(&mutex)!=0){
                        perror("Error pthread_mutex_unlock\n");
                        clean();
                    }
                    written=1;
                }
                break;
            }

            if(pthread_mutex_lock(&mutex)!=0){
                perror("Error pthread_mutex_lock mutex\n");
                clean();
            }

            written=0;  
            int temp = numOfThreads;
            capacitySignal=1;

            if(ctrlc){
                if(pthread_mutex_unlock(&mutex)!=0){
                    perror("Error pthread_mutex_unlock mutex\n");
                    clean();
                }
                break;
            }

            pthread_t* temporary = (pthread_t*) realloc(threads, sizeof(pthread_t)*(numOfThreads+d));
            
            // reallocating threads
            if (temporary == NULL){
                free(threads);                
            }
            
            threads = temporary;

            // initializing the new threads
            for (int i = temp; i < temp+d; ++i) {
                threadInfo[i].threadID=i;
                threadInfo[i].busy=0;

                if(pthread_create(&threads[i], NULL, threadFunc, &threadInfo[i]) < 0){
                    perror("Error while creating threads\n");
                    clean();
                }
            }
            
            types = (type*) realloc(types, sizeof(type)*(numOfThreads+d));

            conditions = (pthread_cond_t*) realloc(conditions, sizeof(pthread_cond_t)*(numOfThreads+d));
            assignment = (pthread_mutex_t*) realloc(assignment, sizeof(pthread_mutex_t)*(numOfThreads+d));

            for (int n = temp; n < numOfThreads+d; ++n) {
                pthread_cond_init(&conditions[n],NULL);
            }

            for (int n = temp; n < numOfThreads+d; ++n) {
                pthread_mutex_init(&assignment[n],NULL);
            }

            numOfThreads+=d;
            limitwritten=0;
            capacitySignal=0;

            if(pthread_mutex_unlock(&mutex)!=0){
                perror("Error pthread_mutex_unlock mutex\n");
                clean();
            }

            if(pthread_mutex_lock(&capacitySignalMutex)!=0){
                perror("Error pthread_mutex_lock mainThread\n");
                clean();
            }

            if(pthread_cond_broadcast(&capacitySignalCond) !=0){
                perror("Error pthread_cond_signal mainCond\n");
                clean();
            }

            if(pthread_mutex_unlock(&capacitySignalMutex)!=0){
                perror("Error pthread_mutex_unlock mainThread\n");
                clean();
            }

            if(ctrlc)
                break;
        }
    }
    void* returnValue=NULL;
    return returnValue;
}

int main(int argc, char *argv[]) {
    int option, PORT = 0;
    char inputFile[MAX];
    int a, readSize;
    struct sockaddr_in server, client;
    char message[MAX];

    pthread_mutex_init(&mutex,NULL);
    pthread_mutex_init(&priorityMutex,NULL);
    pthread_mutex_init(&cacheMutex,NULL);
    pthread_mutex_init(&capacitySignalMutex,NULL);

    if(argc != 13 && argc != 11){
        perror("Error in the number of the given arguments\n");
        exit(-1);
    }

    // ./server -i pathToFile -p PORT -o pathToLogFile -s 4 -x 24 -r 2
    if(argc == 13){
        while((option = getopt(argc, argv, ":i:p:o:s:x:r:")) != -1){
            
            if(option == 'i') {
                if(strcpy(inputFile, optarg)==NULL){
                    perror("Error in arguments\n");
                    exit(-1);
                }
            }

            else if(option == 'p')
                PORT = atoi(optarg);
            else if(option == 'o')
                logFile=optarg;
            else if(option == 's')
                numOfThreads = atoi(optarg);
            else if(option == 'x')
                maxThreads = atoi(optarg);
            else if(option == 'r')
                priority = atoi(optarg);
            else{
                perror( "unknown option\n");
                exit(-1);
            }
        }
        prio=1;
    }
    else if(argc == 11){
        while((option = getopt(argc, argv, ":i:p:o:s:x:")) != -1){
            
            if(option == 'i') {
                if(strcpy(inputFile, optarg)==NULL){
                    perror("Error in arguments\n");
                    exit(-1);
                }
            }

            else if(option == 'p')
                PORT = atoi(optarg);
            else if(option == 'o')
                logFile=optarg;
            else if(option == 's')
                numOfThreads = atoi(optarg);
            else if(option == 'x')
                maxThreads = atoi(optarg);
            else{
                perror( "unknown option\n");
                exit(-1);
            }
        }
    }
    
    instance = open("/tmp/serverFile", O_CREAT|O_EXCL);

    if (instance <0 ){
        perror("There must be only one instance\n");
        exit(1);
    }


    if(maxThreads<numOfThreads){
        perror("Error in the given values");
        if(close(instance)!=0){
            perror("Error close instance\n");
            clean();
        }
        if(remove("/tmp/serverFile")<0){
            perror("Error remove\n");
            clean();
        }
        exit(-1);
    }

    if ((fd = open(inputFile, O_RDONLY, 0)) < 0){
        perror("Error while opening file");
        
        if(close(instance)!=0){
            perror("Error close instance\n");
        }
        if(remove("/tmp/serverFile")<0){
            perror("Error remove\n");
        }
        exit(-1);
    }

    if (truncate(logFile, 0) == -1){
        perror("Error truncate");
    }

    if ((fd2 = open(logFile, O_CREAT |O_RDWR, 0)) < 0){
        perror("Error while opening file");

        if(close(instance)!=0){
            perror("Error close instance\n");
        }
        if(remove("/tmp/serverFile")<0){
            perror("Error remove\n");
        }

        exit(-1);
    }

    int count=0;

    if(pthread_mutex_lock(&mutex)!=0){
        perror("Error pthread_mutex_lock mutex\n");
        if(close(fd2)!=0){
            perror("Error close fd2\n");
        }
        if(close(fd)!=0){
            perror("Error close fd\n");
        }
        if(close(instance)!=0){
            perror("Error close instance\n");
        }
        if(remove("/tmp/serverFile")<0){
            perror("Error remove\n");
        }
        exit(-1);
    }
    count = 0;

    if(prio==0){
        sprintf(logMsg, "Executing with parameters:\n-i %s\n-p %d\n-o %s\n-s %d\n-x %d\n",
            inputFile, PORT, logFile, numOfThreads, maxThreads);
    }
    else{
        sprintf(logMsg, "Executing with parameters:\n-i %s\n-p %d\n-o %s\n-s %d\n-x %d\n-r %d\n",
            inputFile, PORT, logFile, numOfThreads, maxThreads, priority);
    }

    for (int i = 0; logMsg[i]!=0; ++i) {
        count++;
    }
    timeStamp();
    write(fd2, logMsg, count* sizeof(char));
    memset(logMsg, 0, sizeof(char)*MAX);

    if(pthread_mutex_unlock(&mutex)!=0){
        perror("Error pthread_mutex_unlock\n");
        if(close(fd2)!=0){
            perror("Error close fd2\n");
        }
        if(close(fd)!=0){
            perror("Error close fd\n");
        }
        if(close(instance)!=0){
            perror("Error close instance\n");
        }
        if(remove("/tmp/serverFile")<0){
            perror("Error remove\n");
        }
        exit(-1);
    }

    struct sigaction sact;

    sigemptyset(&sact.sa_mask);
    sact.sa_flags = 0;
    sact.sa_handler = catcher;

    if (sigaction(SIGINT, &sact, NULL) != 0)
        perror("SIGINT sigaction() error");
    if (sigaction(SIGTERM, &sact, NULL) != 0)
        perror("SIGINT sigaction() error");

    char c, buffer[MAX];
    char *token = NULL;

    int i=0, numRead,line=0, first=-1, second = 0;

    while ((numRead = read(fd, &c, sizeof(char))) > 0) {

        if (c == '\n') {
            if(buffer[0]=='#') {
                i = 0;
                memset(buffer, 0, sizeof(buffer));
                continue;
            }
            line++;
            i=0;
            continue;
        }
        buffer[i] = c;
        i++;
    }

    if(close(fd)!=0){
        perror("Error close\n");
        if(close(fd2)!=0){
            perror("Error close fd2\n");
        }
        if(close(instance)!=0){
            perror("Error close instance\n");
        }
        if(remove("/tmp/serverFile")<0){
            perror("Error remove\n");
        }
        exit(-1);
    }

    size=line;
    memset(buffer, 0, sizeof(buffer));

    vertices = (vertex*) malloc(sizeof(vertex)*line);
    cache = (cacheDS*) malloc(sizeof(cacheDS)*line);
    visited = (int*) malloc(sizeof(int)*line);
    threadInfo = (threadType*) malloc(sizeof(threadType)*line);
    types = (type*) malloc(sizeof(type)*numOfThreads);
    conditions = (pthread_cond_t*) malloc(sizeof(pthread_cond_t)*numOfThreads);
    assignment = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t)*numOfThreads);

    for (int n = 0; n < numOfThreads; ++n) {
        pthread_cond_init(&conditions[n],NULL);
    }

    for (int n = 0; n < numOfThreads; ++n) {
        pthread_mutex_init(&assignment[n],NULL);
    }

    for (int n = 0; n < line; ++n) {
        cache[n].index=0;
    }

    for (int k = 0; k < line; ++k) {
        visited[k]=0;
    }

    if ((fd = open(inputFile, O_RDONLY, 0)) < 0){
        perror("Error while opening file");

        if(close(fd2)!=0){
            perror("Error close fd2\n");
        }
        if(close(instance)!=0){
            perror("Error close instance\n");
        }
        if(remove("/tmp/serverFile")<0){
            perror("Error remove\n");
        }
        exit(-1);
    }

    for (int l = 0; l < size; ++l) {
        vertices[l].index=0;
    }

    threads = (pthread_t*) malloc (sizeof(pthread_t)* numOfThreads);

    for (int i = 0; i < numOfThreads; ++i) {
        threadInfo[i].threadID=i;
        threadInfo[i].busy=0;

        if(pthread_create(&threads[i], NULL, threadFunc, &threadInfo[i]) < 0){
            perror("Error while creating threads\n");
            clean();
        }
    }

    if(pthread_create(&capacityThread, NULL, capacityChecker, NULL) < 0){
        perror("Error while creating threads\n");
        clean();
    }

    if(pthread_mutex_lock(&mutex)!=0){
        perror("Error pthread_mutex_lock mutex\n");
        clean();
    }
    count = 0;

    sprintf(logMsg, "Loading graph...\n");

    for (i = 0; logMsg[i]!=0; ++i) {
        count++;
    }

    timeStamp();
    write(fd2, logMsg, count* sizeof(char));
    memset(logMsg, 0, sizeof(char)*MAX);

    count = 0;
    if(pthread_mutex_unlock(&mutex)!=0){
        perror("Error pthread_mutex_unlock\n");
        clean();
    }

    clock_t begin = clock();

    while ((numRead = read(fd, &c, sizeof(char))) > 0) {

        if (c == '\n') {
            if(buffer[0]=='#') {
                i=0;
                memset(buffer,0, sizeof(buffer));
                continue;
            }

            token = strtok(buffer, "\t");

            char temp[64];
            char temp1[64];

            while( token != NULL ) {
                if(first==-1){
                    strcpy(temp, token);

                    temp[strlen(token)]='\0';
                    first = atoi(temp);
                }
                else {
                    strcpy(temp1, token);
                    temp1[strlen(token)] = '\0';
                    second = atoi(temp1);
                }
                token = strtok(NULL, "\t");
            }


            int j = check(first);

            if(j > -1){
                vertices[j].children[vertices[j].index] = second;
                vertices[j].index++;
            }
            else {
                if(first==-1)
                    first=0;

                vertices[full].id = first;
                vertices[full].children[vertices[full].index] = second;
                vertices[full].index++;
                full++;
            }

            memset(buffer,0, sizeof(buffer));
            first=-1;

            i=0;
            continue;
        }
        buffer[i] = c;
        i++;
    }

    for (int k = 0; k < full; ++k) {
        totalNeighbors += (vertices[k].index)-1;
    }

    clock_t end = clock();
    double timeSpent = (double)(end - begin) / CLOCKS_PER_SEC;

    if(pthread_mutex_lock(&mutex)!=0){
        perror("Error pthread_mutex_lock mutex\n");
        clean();
    }
    count = 0;

    sprintf(logMsg, "Graph loaded in %.2lf seconds with %d nodes and %d edges. A pool of %d threads has been created\n",
            timeSpent, full, totalNeighbors, numOfThreads);

    for (i = 0; logMsg[i]!=0; ++i) {
        count++;
    }
    
    timeStamp();
    write(fd2, logMsg, count* sizeof(char));
    memset(logMsg, 0, sizeof(char)*MAX);

    if(pthread_mutex_unlock(&mutex)!=0){
        perror("Error pthread_mutex_unlock\n");
        clean();
    }

    socket2 = socket(AF_INET , SOCK_STREAM , 0);
    if (socket2 == -1){
        perror("Error socket");
        clean();
    }
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    if(bind(socket2,(struct sockaddr *)&server , sizeof(server)) < 0){
        perror("Error bind. Exitting...");
        clean();
    }

    listen(socket2 , 3);

    while(ctrlc==0){
        if(pthread_mutex_lock(&mutex)!=0){
            perror("Error pthread_mutex_lock mutex\n");
            clean();
        }
        count = 0;

        sprintf(logMsg, "Waiting for clients...\n");

        for (i = 0; logMsg[i]!=0; ++i) {
            count++;
        }
        timeStamp();
        write(fd2, logMsg, count* sizeof(char));
        memset(logMsg, 0, sizeof(char)*MAX);

        if(pthread_mutex_unlock(&mutex)!=0){
            perror("Error pthread_mutex_unlock\n");
            clean();
        }
        
        if(ctrlc)
            break;

        a = sizeof(struct sockaddr_in);
        clientSocket = accept(socket2, (struct sockaddr *)&client, (socklen_t*)&a);
        if (clientSocket < 0){
            break;
        }

        if(ctrlc)
            break;

        if((readSize = recv(clientSocket, message, MAX, 0)) > 0) {
            type t;
            t.destination=0;
            t.source=0;

            token = strtok(message, " ");
            int f=-1,s2=-1;
            pid_t p;
            char temp[64];
            char temp1[64];
            char temp2[64];

            while(token != NULL ) {
                if(f==-1){
                    strcpy(temp, token);
                    temp[strlen(token)]='\0';
                    f = atoi(temp);
                    if(f<0){
                        printf("invalid value\n");
                        break;
                    }
                    t.source = f;
                }
                else if(s2==-1){
                    strcpy(temp1, token);
                    temp1[strlen(token)] = '\0';
                    s2 = atoi(temp1);
                    if(s2<0){
                        printf("invalid value\n");
                        break;
                    }
                    t.destination = s2;
                }
                else {
                    strcpy(temp2, token);
                    temp2[strlen(token)] = '\0';
                    p = atoi(temp2);
                    t.pid = p;
                }
                token = strtok(NULL, " ");
            }


            if(pthread_mutex_lock(&mutex)!=0){
                perror("Error pthread_mutex_lock mutex\n");
                clean();
            }

            count = 0;

            sprintf(logMsg, "A connection has been delegated to thread id #%d, system load %.5f%%\n",
                    threadIndex, rate);

            for (i = 0; logMsg[i]!=0; ++i) {
                count++;
            }
            timeStamp();
            write(fd2, logMsg, count* sizeof(char));
            memset(logMsg, 0, sizeof(char)*MAX);

            if(pthread_mutex_unlock(&mutex)!=0){
                perror("Error pthread_mutex_unlock\n");
                clean();
            }

            while(threadInfo[threadIndex].busy == 1){
                pass++;

                if(pass==numOfThreads){
                    mainThreadWaiting=1;
                    if(pthread_mutex_lock(&mutex)!=0){
                        perror("Error pthread_mutex_lock mutex\n");
                        clean();
                    }

                    count = 0;

                    sprintf(logMsg, "No thread is available! Waiting for one.\n");

                    for (i = 0; logMsg[i]!=0; ++i) {
                        count++;
                    }
                    timeStamp();
                    write(fd2, logMsg, count* sizeof(char));
                    memset(logMsg, 0, sizeof(char)*MAX);

                    if(pthread_mutex_unlock(&mutex)!=0){
                        perror("Error pthread_mutex_unlock\n");
                        clean();
                    }

                    if(pthread_mutex_lock(&mainThread)!=0){
                        perror("Error pthread_mutex_lock mainThread\n");
                        clean();
                    }
                    if(pthread_cond_wait(&mainCond,&mainThread) !=0){
                        perror("Error pthread_cond_signal mainCond\n");
                        clean();
                    }
                    if(pthread_mutex_unlock(&mainThread)!=0){
                        perror("Error pthread_mutex_unlock mainThread\n");
                        clean();
                    }
                    mainThreadWaiting=0;
                }
                threadIndex++;

                if(threadIndex==numOfThreads)
                    threadIndex=0;
            }

            if(threadIndex==numOfThreads)
                threadIndex=0;

            threadInfo[threadIndex].job = t;
            threadInfo[threadIndex].busy=1;
            threadInfo[threadIndex].clientSocket = clientSocket;
            
            if(pthread_mutex_lock(&assignment[threadIndex])!=0){
                fprintf(stderr,"Error pthread_mutex_lock assignment[threadIndex] (mainThread) %s\n", strerror(errno));
                clean();
            }

            if(pthread_cond_signal(&conditions[threadIndex]) !=0){
                perror("Error pthread_cond_signal conditions[threadIndex]\n");
                clean();
            }

            if(pthread_mutex_unlock(&assignment[threadIndex])!=0){
                perror("Error pthread_mutex_unlock assignment[threadIndex]\n");
                clean();
            }

            threadIndex++;
            pass=0;

            memset(message, 0, sizeof(message));
        }
        else if(readSize == 0) {
            printf("Client disconnected\n");
        }
        else if(readSize == -1){
            if(ctrlc)
                break;
            perror("Error receive");
        }
    }
    
    int d=0;

    while(finishedThreads<numOfThreads){
        if(d==numOfThreads)
            d=0;

        int id=threadInfo[d].threadID;

        if(pthread_mutex_lock(&assignment[id])!=0){
            perror("Error pthread_mutex_lock assignment[id]\n");
            clean();
        }

        if(pthread_cond_signal(&conditions[id]) !=0){
            perror("Error pthread_cond_signal conditions[id]\n");
            clean();
        }


        if(pthread_mutex_unlock(&assignment[id])!=0){
            perror("Error pthread_mutex_unlock assignment[id]\n");
            clean();
        }

        if(pthread_mutex_lock(&mutex)!=0){
            perror("Error pthread_mutex_lock assignment[id]\n");
            clean();
        }
        if(pthread_cond_signal(&capacitySignalCond) !=0){
            perror("Error pthread_cond_signal conditions[id]\n");
            clean();
        }
        if(pthread_mutex_unlock(&mutex)!=0){
            perror("Error pthread_mutex_unlock assignment[id]\n");
            clean();
        }


        d++;
    }

    if(close(fd)!=0){
        perror("Error close\n");
        clean();
    }
    for (i = 0; i < numOfThreads; ++i) {
        if(pthread_join(threads[i], NULL) < 0){
            perror("Error in thread join\n");
            clean();
        }
    }
    if(pthread_join(capacityThread, NULL) < 0){
        perror("Error in thread join\n");
        clean();
    }

    if(pthread_mutex_lock(&mutex)!=0){
        perror("Error pthread_mutex_lock mutex\n");
        clean();
    }

    count = 0;

    sprintf(logMsg, "All threads have terminated, server shutting down.\n");

    for (i = 0; logMsg[i]!=0; ++i) {
        count++;
    }
    
    timeStamp();
    write(fd2, logMsg, count* sizeof(char));
    memset(logMsg, 0, sizeof(char)*MAX);

    if(pthread_mutex_unlock(&mutex)!=0){
        perror("Error pthread_mutex_unlock\n");
        clean();
    }

    free(vertices);
    free(visited);
    free(threads);
    free(cache);
    free(types);
    free(conditions);
    free(assignment);
    free(threadInfo);

    if(close(clientSocket)!=0){
        //
    }

    if(close(socket2)!=0){
        perror("Error close socket2\n");
    }

    if(close(fd2)!=0){
        perror("Error close fd2\n");
    }
    if(close(instance)!=0){
        perror("Error close instance\n");
    }

    if(remove("/tmp/serverFile")<0){
        perror("Error remove\n");
    }

    return 0;
}

int check(int id){
    for (int i = 0; i < size; ++i) {
        if(vertices[i].id==id){
            return i;
        }
    }
    return -1;
}

int BFS(int source, int destination, int ID){
    int count=0;
    for (int k = 0; k < size; ++k) {
        visited[k]=0;
    }
 
    if(priority == 1 && writers>0){
        if(pthread_mutex_lock(&mutex)!=0){
            perror("Error pthread_mutex_lock mutex\n");
            clean();
        }

        int count = 0,i ;
        sprintf(logMsg, "Reader threads are waiting.\n");

        for (i = 0; logMsg[i]!=0; ++i) {
            count++;
        }
        
        timeStamp();
        write(fd2, logMsg, count* sizeof(char));
        memset(logMsg, 0, sizeof(char)*MAX);

        if(pthread_mutex_unlock(&mutex)!=0){
            perror("Error pthread_mutex_unlock\n");
            clean();
        }

        if(pthread_mutex_lock(&priorityMutex)!=0){
            perror("Error pthread_mutex_lock priorityMutex\n");
            clean();
        }

        if(pthread_cond_wait(&priorityCond, &priorityMutex)<0){
            perror("Error pthread_cond_wait priorityCond\n");
            clean();
        }

        if(pthread_mutex_unlock(&priorityMutex)!=0){
            perror("Error pthread_mutex_unlock priorityMutex\n");
            clean();
        }
    }

    if(priority==0)
        readers++;

    // check if the path already exists
    for (int i = 0; i < cacheSize; ++i) {
        if(cache[i].source == source && cache[i].destination == destination) {            
            if(pthread_mutex_lock(&mutex)!=0){
                perror("Error pthread_mutex_lock mutex\n");
                clean();
            }

            count = 0;
            sprintf(logMsg, "Thread #%d: path found in database: \n", ID);

            for (int j = 0; logMsg[j]!=0; ++j) {
                count++;
            }
            
            timeStamp();
            write(fd2, logMsg, count* sizeof(char));
            memset(logMsg, 0, sizeof(char)*MAX);

            if(pthread_mutex_unlock(&mutex)!=0){
                perror("Error pthread_mutex_unlock\n");
                clean();
            }
            return i;
        }
    }

    if(priority==0)
        readers--;

    if(priority == 0 && readers<1){
        unlocking();
    }

    if(pthread_mutex_lock(&mutex)!=0){
        perror("Error pthread_mutex_lock mutex\n");
        clean();
    }

    count = 0;
    sprintf(logMsg, "no path in database, calculating %d->%d\n", source, destination);
    for (int i = 0; logMsg[i]!=0; ++i) {
        count++;
    }
    
    timeStamp();
    write(fd2, logMsg, count* sizeof(char));
    memset(logMsg, 0, sizeof(char)*MAX);

    if(pthread_mutex_unlock(&mutex)!=0){
        perror("Error pthread_mutex_unlock\n");
        clean();
    }

    if(source==destination){        

        if(priority == 0 && readers>0){

            if(pthread_mutex_lock(&mutex)!=0){
                perror("Error pthread_mutex_lock mutex\n");
                clean();
            }

            count = 0;
            sprintf(logMsg, "Writer threads are waiting.\n");
            for (int i = 0; logMsg[i]!=0; ++i) {
                count++;
            }
            
            timeStamp();
            write(fd2, logMsg, count* sizeof(char));
            memset(logMsg, 0, sizeof(char)*MAX);

            if(pthread_mutex_unlock(&mutex)!=0){
                perror("Error pthread_mutex_unlock\n");
                clean();
            }

            if(pthread_mutex_lock(&priorityMutex)!=0){
                perror("Error pthread_mutex_lock priorityMutex\n");
                clean();
            }

            if(pthread_cond_wait(&priorityCond,&priorityMutex)<0){
                perror("Error pthread_cond_wait priorityCond\n");
                clean();
            }

            if(pthread_mutex_unlock(&priorityMutex)!=0){
                perror("Error pthread_mutex_unlock priorityMutex\n");
                clean();
            }
        }

        if(pthread_mutex_lock(&cacheMutex)!=0){
            perror("Error pthread_mutex_lock cacheMutex\n");
            clean();
        }

        writers++;
        cache[cacheIndex].source=source;
        cache[cacheIndex].destination=destination;
        cache[cacheIndex].path[0]=source;
        cache[cacheIndex].path[1]=destination;
        cache[cacheIndex].index=2;
        cacheIndex++;
        cacheSize++;
        writers--;

        if(pthread_mutex_unlock(&cacheMutex)!=0){
            perror("Error pthread_mutex_unlock cacheMutex\n");
            clean();
        }

        if(priority == 1 && writers<1){
            unlocking();
        }

        return cacheIndex-1;
    }

    int head = 0, queueCounter=0, queueSize = 0, vert, pathCounter=0;
    int **queue = (int**) calloc(totalNeighbors, sizeof(int*));
    int *path = (int*) calloc(totalNeighbors, sizeof(int));

    for (int i = 0; i < totalNeighbors; i++ ){
        queue[i] = (int*) calloc(totalNeighbors, sizeof(int));
    }

    for (int i = 0; i < totalNeighbors; ++i)
        for (int j = 0; j < totalNeighbors; ++j)
            queue[i][j]=-1;

    path[pathCounter++]=source;

    for (int i = 0; i < pathCounter; ++i)
    {
        queue[queueCounter][i]=path[i];
    }
    queueCounter++;
    queueSize++;

    while(head<totalNeighbors){

        for (int i = 0; queue[head][i]!=-1; ++i){
            path[i]=queue[head][i];
            pathCounter = i;
        }

        head++;
        vert = path[pathCounter];
        if (vert == destination){
            if(priority == 0 && readers>0){

                if(pthread_mutex_lock(&mutex)!=0){
                    perror("Error pthread_mutex_lock mutex\n");
                    clean();
                }

                count = 0;
                sprintf(logMsg, "Writer threads are waiting.\n");
                for (int i = 0; logMsg[i]!=0; ++i) {
                    count++;
                }
                
                timeStamp();
                write(fd2, logMsg, count* sizeof(char));
                memset(logMsg, 0, sizeof(char)*MAX);

                if(pthread_mutex_unlock(&mutex)!=0){
                    perror("Error pthread_mutex_unlock\n");
                    clean();
                }

                if(pthread_mutex_lock(&priorityMutex)!=0){
                    perror("Error pthread_mutex_lock priorityMutex\n");
                    clean();
                }

                if(pthread_cond_wait(&priorityCond,&priorityMutex)<0){
                    perror("Error pthread_cond_wait priorityCond\n");
                    clean();
                }

                if(pthread_mutex_unlock(&priorityMutex)!=0){
                    perror("Error pthread_mutex_unlock priorityMutex\n");
                    clean();
                }
            }

            if(pthread_mutex_lock(&cacheMutex)!=0){
                perror("Error pthread_mutex_lock cacheMutex\n");
                clean();
            }
            writers++;
            // copy path into the cache
            int i=0;
            for (i = 0; i < pathCounter+1; ++i){
                cache[cacheIndex].path[i]=path[i];
            }

            cache[cacheIndex].index=i;
            cache[cacheIndex].source=source;
            cache[cacheIndex].destination=destination;

            cacheIndex++;
            cacheSize++;
            writers--;

            for ( i = 0; i < totalNeighbors; i++){
                free(queue[i]);
            }

            free(queue);
            free(path);

            if(pthread_mutex_unlock(&cacheMutex)!=0){
                perror("Error pthread_mutex_unlock cacheMutex\n");
                clean();
            }

            if(priority == 1 && writers<1){
                unlocking();
            }

            return cacheIndex-1;
        }

        if(isVisited(vert))
            continue;

        int g=0;
        for (int i = 0; i < getVertex(vert).index; ++i){
            int *newPath = (int*) calloc(totalNeighbors, sizeof(int));
            for (g = 0; g < pathCounter+1; ++g){
                newPath[g]=path[g];
            }

            newPath[g]= getVertex(vert).children[i];
            for (int l = 0; l < g+1; ++l){
                queue[queueCounter][l]=newPath[l];
            }

            queueCounter++;
            queueSize++;
            free(newPath);
        }

        visited[vert]=1;
    }

    if(pthread_mutex_lock(&cacheMutex)!=0){
        perror("Error pthread_mutex_lock cacheMutex\n");
        clean();
    }

    if(priority == 0 && readers>0){
        if(pthread_mutex_lock(&mutex)!=0){
            perror("Error pthread_mutex_lock mutex\n");
            clean();
        }

        count = 0;
        sprintf(logMsg, "Writer threads are waiting.\n");
        for (int i = 0; logMsg[i]!=0; ++i) {
            count++;
        }
        
        timeStamp();
        write(fd2, logMsg, count* sizeof(char));
        memset(logMsg, 0, sizeof(char)*MAX);

        if(pthread_mutex_unlock(&mutex)!=0){
            perror("Error pthread_mutex_unlock\n");
            clean();
        }
        if(pthread_mutex_lock(&priorityMutex)!=0){
            perror("Error pthread_mutex_lock priorityMutex\n");
            clean();
        }

        if(pthread_cond_wait(&priorityCond,&priorityMutex)<0){
            perror("Error pthread_cond_wait priorityCond\n");
            clean();
        }

        if(pthread_mutex_unlock(&priorityMutex)!=0){
            perror("Error pthread_mutex_unlock priorityMutex\n");
            clean();
        }
    }

    writers++;

    cache[cacheIndex].source=source;
    cache[cacheIndex].destination=destination;
    cache[cacheIndex].path[0]=-1;
    cache[cacheIndex].index=1;
    cacheIndex++;
    cacheSize++;

    for (int i = 0; i < totalNeighbors; i++){
        free(queue[i]);
    }
    writers--;

    free(queue);
    free(path);

    if(pthread_mutex_unlock(&cacheMutex)!=0){
        perror("Error pthread_mutex_unlock cacheMutex\n");
        clean();
    }

    return -1;
}

vertex getVertex(int source){
    for (int i = 0; i < size; ++i) {
        if(vertices[i].id==source)
            return vertices[i];
    }
    vertex v;
    v.id = -1;
    return v;
}

int getIndex(int source){
    for (int i = 0; i < size; ++i) {
        if(vertices[i].id==source)
            return i;
    }
    return -1;
}

int isVisited(int num){
    for (int i = 0; i < size; ++i) {
        if(vertices[i].id==num) {
            if(visited[i]==0){
                return 0;
            }
            else
                return 1;
        }
    }
    return -1;
}

void catcher(int signum) {
    int count=0;
    if (signum == SIGINT) {
        if(pthread_mutex_lock(&mutex)!=0){
            perror("Error pthread_mutex_lock mutex\n");
            clean();
        }
        count = 0;
        timeStamp();
        sprintf(logMsg, "Termination signal received, waiting for ongoing threads to complete.\n");

        for (int i = 0; logMsg[i]!=0; ++i) {
            count++;
        }
        timeStamp();
        write(fd2, logMsg, count* sizeof(char));
        memset(logMsg, 0, sizeof(char)*MAX);

        if(pthread_mutex_unlock(&mutex)!=0){
            perror("Error pthread_mutex_unlock\n");
            clean();
        }
        ctrlc=1;
    }
    else if (signum == SIGTERM) {
        ctrlc=1;
        if(pthread_mutex_lock(&mutex)!=0){
            perror("Error pthread_mutex_lock mutex\n");
            clean();
        }
        count = 0;
        
        timeStamp();
        sprintf(logMsg, "Termination signal received, waiting for ongoing threads to complete.\n");

        for (int i = 0; logMsg[i]!=0; ++i) {
            count++;
        }
        timeStamp();
        write(fd2, logMsg, count* sizeof(char));
        memset(logMsg, 0, sizeof(char)*MAX);

        if(pthread_mutex_unlock(&mutex)!=0){
            perror("Error pthread_mutex_unlock\n");
            clean();
        }
    }
}

void clean(){
    ctrlc=1;
    int i=0;

    //************************************************************* bu dogru mu? trylock??*********
    if(pthread_mutex_unlock(&mutex)!=0){
        perror("Error pthread_mutex_unlock mutex\n");
    }

    if(pthread_mutex_unlock(&priorityMutex)!=0){
        perror("Error pthread_mutex_lock priorityMutex\n");
        clean();
    }

    if(close(socket2)!=0){
        perror("Error close socket2\n");
    }

    if(close(fd)!=0){
        perror("Error close\n");
    }
    for (i = 0; i < numOfThreads; ++i) {
        threadInfo[i].busy=1;
        if(pthread_mutex_lock(&assignment[i])!=0){
            perror("Error pthread_mutex_lock assignment[threadIndex] (mainThread)\n");
        }

        if(pthread_cond_signal(&conditions[i]) !=0){
            perror("Error pthread_cond_signal conditions[threadIndex]\n");
        }

        if(pthread_mutex_unlock(&assignment[i])!=0){
            perror("Error pthread_mutex_unlock assignment[threadIndex]\n");
        }

        if(pthread_join(threads[i], NULL) < 0){
            perror("Error in thread join\n");
        }
    }

    if(pthread_join(capacityThread, NULL) < 0){
        perror("Error in thread join\n");
    }

    free(vertices);
    free(visited);
    free(threads);
    free(cache);
    free(types);
    free(conditions);
    free(assignment);
    free(threadInfo);

    if(close(clientSocket)!=0){
        perror("Error close clientSocket\n");
    }
    if(close(fd2)!=0){
        perror("Error close fd2\n");
    }
    if(close(instance)!=0){
        perror("Error close instance\n");
    }
    if(remove("/tmp/serverFile")<0){
        perror("Error remove\n");
    }

    exit(-1);
}

void unlocking(){
    if(priority==0){
        if(pthread_mutex_unlock(&priorityMutex)!=0){
            perror("Error pthread_mutex_lock priorityMutex\n");
            clean();
        }

        if(pthread_cond_broadcast(&priorityCond)<0){
            perror("Error pthread_cond_broadcast priorityCond\n");
            clean();
        }

        if(pthread_mutex_unlock(&priorityMutex)!=0){
            perror("Error pthread_mutex_unlock priorityMutex\n");
            clean();
        }
    }
    else if(priority ==1){
        if(pthread_mutex_unlock(&priorityMutex)!=0){
            perror("Error pthread_mutex_lock priorityMutex\n");
            clean();
        }

        if(pthread_cond_broadcast(&priorityCond)<0){
            perror("Error pthread_cond_broadcast priorityCond\n");
            clean();
        }

        if(pthread_mutex_unlock(&priorityMutex)!=0){
            perror("Error pthread_mutex_unlock priorityMutex\n");
            clean();
        }
    }
}