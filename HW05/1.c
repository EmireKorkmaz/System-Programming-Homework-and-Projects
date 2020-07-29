#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <math.h>

#define MAX(x,y) (((x) > (y)) ? (x) : (y))
#define INITIAL_FLOWERS 10

pthread_mutex_t *mutexes;
pthread_cond_t *conditions;
pthread_t *threads;

int threadPool=0, done=0, fd, ctrlc=0, NUM_OF_FLORISTS=10, NUM_OF_CLIENTS=500;

typedef struct client{
    char name[15];
    double distance;
    double x;
    double y;
    char flower[INITIAL_FLOWERS];
} client;

typedef struct florist{
    char name[15];
    int id;
    double x;
    double y;
    int sales;
    double time;
    int busy;
    pthread_cond_t cond;
    double speed;
    char flowers[INITIAL_FLOWERS][INITIAL_FLOWERS];
    client clients[500];
    int i;
    int clientNumber;
}florist;

typedef struct type{
    double max;
    int index;
}type;

florist* florists;
client* clients;

void* threadFunc(void* t);
type getAvailableFlorist(double x, double y, char flower[]);
void catcher(int signum);

int main(int argc, char *argv[]) {

    int i=0, line=0, l=0, floristsTurn=1, k=0, option;

    char *filename = NULL;

    if(argc != 3){
        fprintf(stderr,"Error in the number of the given arguments\n");
        exit(-1);
    }

    while((option = getopt(argc, argv, ":i:")) != -1){
        if(option == 'i')
            filename = optarg;
        else{
            fprintf(stderr, "unknown option\n");
            exit(1);
        }
    }

    if ((fd = open(filename, O_RDONLY, 0)) < 0){
        fprintf(stderr,"Error while opening file");
        exit(-1);
    }

    struct sigaction sact;

    sigemptyset(&sact.sa_mask);
    sact.sa_flags = 0;
    sact.sa_handler = catcher;

    if (sigaction(SIGINT, &sact, NULL) != 0)
        perror("SIGINT sigaction() error");

    char buffer[80];
    const char s[7] = " (,;):";
    char *token;

    int numOfFlorists=0, numOfClients=0;

    florists = (florist *) malloc(sizeof(florist)*NUM_OF_FLORISTS); // initially there are five florists
    conditions = (pthread_cond_t *) malloc(sizeof(pthread_cond_t)*NUM_OF_FLORISTS); // conds for florists
    mutexes = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t)*NUM_OF_FLORISTS); // mutexes florists
    clients = (client *) malloc(sizeof(client)*NUM_OF_CLIENTS); // initially there are twenty five clients

    for (int n = 0; n < NUM_OF_FLORISTS; ++n) {
        pthread_cond_init(&conditions[n],NULL);
    }

    for (int n = 0; n < NUM_OF_FLORISTS; ++n) {
        pthread_mutex_init(&mutexes[n],NULL);
    }

    memset(buffer,0,80);
    int loop=0;

    printf("Florist application initializing from file: %s\n", filename);

    while ((read(fd, &buffer[i], 1) == 1 && ctrlc==0)) {

        if (buffer[i] == '\n') {
            loop++;

            // doubles the number of florists if needed
            if(line>NUM_OF_FLORISTS){
                florists = (florist*) realloc(florists, NUM_OF_FLORISTS*2);
            }

            if(strlen(buffer)>1 && floristsTurn==1){ // florist side
                numOfFlorists++;
                int j=0;
                token = strtok(buffer, s);

                while( token != NULL ) {
                    if(j==0)
                        strcpy(florists[line].name, token);
                    else if(j==1){
                        char temp[10];
                        strcpy(temp, token);
                        florists[line].x =strtof(temp,NULL);
                    }
                    else if(j==2){
                        char temp[10];
                        strcpy(temp, token);
                        florists[line].y = strtof(temp,NULL);
                    }
                    else if(j==3){
                        char temp[10];
                        strcpy(temp, token);

                        temp[strlen(token)]='\0';
                        florists[line].speed = strtof(temp,NULL);

                    }
                    else if(j>3){
                        char temp[15];
                        memset(temp,0,15);

                        for (int m = 0; token[m]!='\n'; ++m) {
                            florists[line].flowers[k][m] = token[m];
                        }
                        k++;
                    }

                    j++;
                    token = strtok(NULL, s);
                }
                memset(buffer,0,80);

                florists[line].cond=conditions[line];
                florists[line].id=line;
                florists[line].time= 0.0;
                florists[line].sales=0;
                florists[line].i=0;
                florists[line].busy=0;
                florists[line].clientNumber=0;

                for (int n = 0; n < NUM_OF_CLIENTS*2 ; ++n) {
                    strcpy(florists[line].clients[n].name,"");
                }

                i=0;
                line++;
                k=0;
                continue;
            }
            else if(strlen(buffer)==1 && threadPool == 0){
                floristsTurn = 0;
                printf("%d florists have been created.\n", numOfFlorists);
                printf("Processing requests\n");

                threads = (pthread_t*) malloc (sizeof(pthread_t)*numOfFlorists);

                for (i = 0; i < numOfFlorists; ++i) {
                    if(pthread_create(&threads[i], NULL, threadFunc, &florists[i]) < 0){
                        fprintf(stderr,"Error while creating threads\n");
                        exit(-1);
                    }
                }

                NUM_OF_FLORISTS = numOfFlorists;

                memset(buffer,0,80);
                i=0;
                threadPool=1;
                continue;
            }
            else if(strlen(buffer)==1 && threadPool == 1){
                continue;
            }
            else{  // client side
                numOfClients++;
                int j=0;
                token = strtok(buffer, s);

                while( token != NULL ) {
                    if(j==0)
                        strcpy(clients[l].name, token);
                    else if(j==1){
                        char temp[10];
                        strcpy(temp, token);
                        clients[l].x = strtof(temp,NULL);
                    }
                    else if(j==2){
                        char temp[10];
                        strcpy(temp, token);
                        clients[l].y = strtof(temp,NULL);
                    }
                    else if(j==3){
                        char temp[10];
                        strcpy(temp, token);
                        strcpy(clients[l].flower,temp);
                        k++;
                    }
                    j++;
                    token = strtok(NULL, s);
                }

                type t = getAvailableFlorist(clients[l].x,clients[l].y, clients[l].flower);

                int index = t.index;
                int m=0;

                for (int n = 0; n < NUM_OF_CLIENTS; ++n) {
                    if(strcmp(florists[index].clients[n].name,"")==0){
                        m=n;
                        break;
                    }
                }

                strcpy(florists[index].clients[m].name,clients[l].name);
                florists[index].clients[m].x=clients[l].x;
                florists[index].clients[m].y=clients[l].y;
                florists[index].clients[m].distance = t.max;
                strncpy(florists[index].clients[m].flower,clients[l].flower,strlen(clients[l].flower)-1);
                florists[index].clientNumber = florists[index].clientNumber+1;

//                if(florists[index].busy==0) {
                    if(pthread_mutex_lock(&mutexes[florists[index].id])!=0){
                        fprintf(stderr,"Error pthread_mutex_lock\n");
                        exit(-1);
                    }
                    if(pthread_cond_signal(&florists[index].cond) !=0){
                        fprintf(stderr,"Error pthread_cond_signal\n");
                        exit(-1);
                    }
                    if(pthread_mutex_unlock(&mutexes[florists[index].id])!=0){
                        fprintf(stderr,"Error pthread_mutex_unlock\n");
                        exit(-1);
                    }
//                }

                memset(buffer,0,80);
                l++;
                i=0;
                continue;
            }
        }
        i++;
    }
    done=1;

    int remaining = 0;
    i=0;

    while (ctrlc==0){

        if(i == numOfFlorists){
            if(remaining == 0)
                break;
            remaining=0;
            i=0;
        }

        if(pthread_mutex_lock(&mutexes[florists[i].id])!=0){
            fprintf(stderr,"Error pthread_mutex_lock\n");
            exit(-1);
        }

        if(pthread_cond_signal(&florists[i].cond) !=0){
            fprintf(stderr,"Error pthread_cond_signal\n");
            exit(-1);
        }

        if(pthread_mutex_unlock(&mutexes[florists[i].id])!=0){
            fprintf(stderr,"Error pthread_mutex_unlock\n");
            exit(-1);
        }

        remaining +=florists[i].clientNumber;

        i++;
    }

    if(ctrlc==0){
        printf("All requests processed.\n");
    }
    else{
        printf("Not all requests processed.\n");
    }

    for (i = 0; i < numOfFlorists; ++i) {
        if(pthread_join(threads[i], NULL) < 0){
            fprintf(stderr,"Error in thread join\n");
            exit(-1);
        }
    }

    printf("\nSale statistics for today:\n"
           "-------------------------------------------------\n"
           "Florist\t\t"
           "# of sales\t\t"
           "Total time\n"
           "-------------------------------------------------\n");

    for (i = 0; i < numOfFlorists; ++i) {
        printf("%s\t\t%d\t\t\t\t%.1f\n",florists[i].name, florists[i].sales, florists[i].time);
    }

    for (int c = 0; c < NUM_OF_FLORISTS; ++c) {
        if(pthread_mutex_unlock(&mutexes[c]) && pthread_mutex_destroy(&mutexes[c])!=0){
            fprintf(stderr,"Error pthread_mutex_destroy\n");
            exit(-1);
        }
    }

    for (int c = 0; c < NUM_OF_FLORISTS; ++c) {
        if(pthread_cond_destroy(&conditions[c])!=0){
            fprintf(stderr,"Error pthread_cond_destroy\n");
            exit(-1);
        }
    }

    free(florists);
    free(conditions);
    free(mutexes);
    free(threads);
    free(clients);

    if(close(fd)!=0){
        fprintf(stderr,"Error close\n");
        exit(-1);
    }

    return 0;
}

void* threadFunc(void* t){
    florist* f = (florist*) t;

    while(ctrlc == 0 && (done == 0 || f->clientNumber>0)){

        if(f->clientNumber==0)
            continue;
        if(pthread_mutex_lock(&mutexes[f->id])!=0){
            fprintf(stderr,"Error pthread_mutex_lock\n");
            exit(-1);
        }
        if(pthread_cond_wait(&f->cond, &mutexes[f->id])!=0){
            fprintf(stderr,"Error pthread_cond_wait\n");
            exit(-1);
        }

        f->busy=1;

        double d = f->clients[f->i].distance/f->speed;
        srand(getpid());
        int r = (rand() % 250 +1);

        f->time += (r+d);
        f->sales = f->sales+1;
        //usleep((r+d)*1000);

        printf("Florist %s has delivered a %s to %s in %.1f ms\n",f->name, f->clients[f->i].flower, f->clients[f->i].name, (r+d));
       
        f->busy=0;

        f->i = f->i+1;
        f->clientNumber=f->clientNumber-1;

        if(pthread_mutex_unlock(&mutexes[f->id]) != 0){
            fprintf(stderr,"Error pthread_mutex_unlock\n");
            exit(-1);
        }
    }

    printf("%s closing shop.\n", f->name);
    pthread_exit(NULL);
}

// Max(abs(x2 - x1), abs(y2 - y1));
type getAvailableFlorist(double x, double y, char flower[]){
    type t;
    double max=0.0, value=0.0;
    int retVal=0, worked=0;
    max = (MAX(fabs(florists[0].x-x), fabs(florists[0].y- y)));

    for (int i = 0; i < NUM_OF_FLORISTS; ++i) {
         for (int j = 0; j < INITIAL_FLOWERS; ++j) {
                if (strncmp(florists[i].flowers[j], flower, sizeof(char)*3) == 0) {
                    value = (MAX(fabs(florists[i].x - x), fabs(florists[i].y - y)));
                    if(max >= value) {
                        max = value;
                        retVal = i;
                        worked=1;
                    }
                }
         }
    }

    while (worked==0){
        for (int i = 0; i < NUM_OF_FLORISTS; ++i) {
            for (int j = 0; j < INITIAL_FLOWERS; ++j) {
                if (strncmp(florists[i].flowers[j], flower, sizeof(char)*3) == 0) {
                    max = (MAX(fabs(florists[i].x - x), fabs(florists[i].y - y)));
                    retVal = i;
                    worked=1;
                }
            }
        }
    }

    t.max= max;
    t.index = retVal;
    return t;
}

void catcher(int signum) {

    if (signum == SIGINT) {
        ctrlc=1;
        puts("SIGINT is caught. Exiting...\n");
    }
}