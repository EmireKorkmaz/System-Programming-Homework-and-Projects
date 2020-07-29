#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>
#include <fcntl.h>
#include <printf.h>

typedef struct chefType{
    sem_t *mutex;
    sem_t *dessertMutex;
    sem_t *ingredientMutex;
    int id, ingredient1, ingredient2;
    int *shm;
    int *begin;
    int *ingredientsEmpty;
    int *done;
    int *desserts;
    unsigned int *seed;
} chefType;

char* ingredientName(int index);

void * chefs_func(void *t)
{
    int written=0;
    int p=0;
    chefType* ct = (chefType*) t;
    while(1) {
        if (written == 0) {
            fprintf(stdout, "Chef%d is waiting for %s and %s.\n", (ct->id) + 1, ingredientName((ct->ingredient1)), ingredientName((ct->ingredient2)));
            written = 1;
        }
        if (sem_wait(ct->mutex) < 0) {
            fprintf(stderr, "Error sem_wait mutex %s\n", strerror(errno));
            exit(-1);
        }

        if (ct->shm[(ct->ingredient1)] > 0 && ct->shm[(ct->ingredient2)] > 0) {
            (ct->shm[(ct->ingredient1)])--;
            (ct->shm[(ct->ingredient2)])--;
            fprintf(stdout, "Chef%d has taken the %s.\n", (ct->id) + 1, ingredientName((ct->ingredient1)));
            fprintf(stdout, "Chef%d has taken the %s.\n", (ct->id) + 1, ingredientName((ct->ingredient2)));
            p=2;
        }

        if (sem_post(ct->mutex) < 0) {
            fprintf(stderr, "Error sem_wait mutex %s\n", strerror(errno));
            exit(-1);
        }

        if (p == 2) {
            fprintf(stdout, "Chef%d is preparing the dessert.\n", (ct->id) + 1);
            p = 0;
            written = 0;
            if (sem_wait(ct->dessertMutex) < 0) {
                fprintf(stderr, "Error sem_wait mutex %s\n", strerror(errno));
                exit(-1);
            }
            *(ct->desserts) = *(ct->desserts) + 1;
            sleep(rand_r((ct->seed)) % 5 + 1);
            fprintf(stdout, "Chef%d has delivered the dessert to the wholesaler\n", (ct->id) + 1);
            *(ct->begin) = 1;
            if (sem_post(ct->dessertMutex) < 0) {
                fprintf(stderr, "Error sem_post mutex %s\n", strerror(errno));
                exit(-1);
            }

        }

        if (*(ct->ingredientsEmpty) == 1 && (ct->shm[(ct->ingredient1)] == 0 && ct->shm[(ct->ingredient2)] == 0)) {
            fprintf(stdout, "Ingredients have run out for Chef%d\n", (ct->id) + 1);
            *(ct->done) = *(ct->done) + 1;
            break;
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {

    int option;
    char* filename="\0";

    sem_t mutex;
    sem_t dessertMutex;
    sem_t ingredientMutex;

    if(argc != 3){
        fprintf(stderr, "Number of arguments should be 3.");
        exit(-1);
    }

    if (sem_init(&mutex, 0, 1) < 0) {
        fprintf(stderr, "Error sem_init\n");
        exit(-1);
    }
    if (sem_init(&dessertMutex, 0, 1) < 0) {
        fprintf(stderr, "Error sem_init\n");
        exit(-1);
    }

    if (sem_init(&ingredientMutex, 0, 1) < 0) {
        fprintf(stderr, "Error sem_init\n");
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

    struct stat st;
    if (stat(filename, &st) < 0) {
        fprintf(stderr, "Error stat\n");
        exit(-1);
    }
    int minimumSize = 10*3*sizeof(char)-1;
    int size = st.st_size;

    if (size == 0) {
        fprintf(stderr, "The given file should not be empty\n");
        exit(-1);
    }

    if (size < minimumSize) {
        fprintf(stderr, "The given file must contain at least 10 lines and each must contain 2 distinct ingredients.\n");
        exit(-1);
    }
    /*
      milk (M)
      flour (F)
      walnuts (W)
      sugar (S)
     */

    int array[4];
    int begin;
    int ingredientsEmpty;
    int desserts;
    int done;

    begin = 0;
    ingredientsEmpty = 0;
    desserts = 0;
    done = 0;

    chefType chefsType[6];

    for (int k = 0; k < 4; ++k) {
        array[k] = 0;
    }

    int ing[12];
    // MS MF MW SF SW FW

    ing[0] = 0;
    ing[1] = 3;
    ing[2] = 1;
    ing[3] = 0;
    ing[4] = 2;
    ing[5] = 0;
    ing[6] = 3;
    ing[7] = 1;
    ing[8] = 2;
    ing[9] = 3;
    ing[10] = 1;
    ing[11] = 2;


    int k=0;
    unsigned r3;
    srand(getpid());
    for (int j = 0; j < 6; ++j) {
        chefsType[j].shm = array;
        chefsType[j].id = j;
        chefsType[j].mutex = &mutex;
        chefsType[j].ingredient1 = ing[k];
        chefsType[j].ingredient2 = ing[k+1];
        chefsType[j].begin = &begin;
        chefsType[j].ingredientsEmpty = &ingredientsEmpty;
        chefsType[j].dessertMutex = &dessertMutex;
        chefsType[j].ingredientMutex = &ingredientMutex;
        chefsType[j].desserts = &desserts;
        chefsType[j].done = &done;
        r3 = rand() %5+1;
        chefsType[j].seed = &r3;
        k+=2;
    }

    pthread_t chefs[6];

    for (int i = 0; i < 6; ++i) {
        if(pthread_create(&chefs[i], NULL, chefs_func, &chefsType[i]) < 0){
            fprintf(stderr,"Error while creating threads\n");
            exit(-1);
        }
    }

    int fd = open(filename, O_RDONLY);

    if (fd < 0) {
        fprintf(stderr,"Error while opening the input file\n");
        exit(-1);
    }
    int numRead = 0, i = 0, index1=0, index2=0;
    char c, pair[2];
    while ((numRead = read(fd, &c, sizeof(char))) > 0) {

        if(i == 2 && c == '\n') {

            if(pair[0] == pair[1]){
                i=0;
                continue;
            }

            if (sem_wait(&mutex) < 0) {
                fprintf(stderr, "Error sem_wait mutex %s\n", strerror(errno));
                exit(-1);
            }

            if(pair[0] == 'M' || pair[0] == 'm') {
                array[0]++;
                index1= 0;
            }
            else if(pair[0] == 'F' || pair[0] == 'f') {
                array[1]++;
                index1= 1;
            }
            else if (pair[0] == 'W' || pair[0] == 'w') {
                array[2]++;
                index1= 2;
            }
            else if (pair[0] == 'S' || pair[0] == 's') {
                array[3]++;
                index1= 3;
            }

            if(pair[1] == 'M' || pair[1] == 'm') {
                array[0]++;
                index2 = 0;
            }
            else if(pair[1] == 'F' || pair[1] == 'f') {
                array[1]++;
                index2 = 1;
            }
            else if (pair[1] == 'W' || pair[1] == 'w') {
                array[2]++;
                index2 = 2;
            }
            else if (pair[1] == 'S' || pair[1] == 's') {
                array[3]++;
                index2 = 3;
            }

            fprintf(stdout, "The wholesaler delivers %s and %s.\n", ingredientName(index1), ingredientName(index2));

            if (sem_post(&mutex) < 0) {
                fprintf(stderr, "Error sem_wait mutex %s\n", strerror(errno));
                return -1;
            }
            i=0;
            printf("the wholesaler is waiting for the dessert\n");

            while(1){
                if(desserts==0 || begin ==0){
                    continue;
                }
                if (sem_wait(&dessertMutex) < 0) {
                    fprintf(stderr, "Error sem_wait mutex %s\n", strerror(errno));
                    exit(-1);
                }
                desserts--;
                begin = 0;
                fprintf(stdout, "The wholesaler has obtained the dessert and left to sell it.\n");
                if (sem_post(&dessertMutex) < 0) {
                    fprintf(stderr, "Error sem_post mutex %s\n", strerror(errno));
                    exit(-1);
                }
                break;

            }

            continue;
        }
        else if(i>2){
            fprintf(stderr,"Error! Every line should contain only 2 letters\n");
            exit(-1);
        }
        else if(i<2 && c=='\n'){
            fprintf(stderr,"Error! Every line should contain only 2 letters\n");
            exit(-1);
        }

        pair[i] = c;
        i++;



    }
    ingredientsEmpty = 1;

    fprintf(stdout, "Wholesaler : Ingredients have run out. \n");

    while(1){
        if(done < 6 || desserts> 0) {
            if ((desserts) > 0) {
                printf("the wholesaler is waiting for the dessert\n");
                if (sem_wait(&mutex) < 0) {
                    fprintf(stderr, "Error sem_wait mutex %s\n", strerror(errno));
                    exit(-1);
                }
                desserts=0;
                fprintf(stdout, "The wholesaler has obtained the dessert and left to sell it.\n");

                if (sem_post(&mutex) < 0) {
                    fprintf(stderr, "Error sem_post mutex %s\n", strerror(errno));
                    exit(-1);
                }
            }
        }
        else if(done==6 && desserts==0)
            break;
    }

    for (i = 0; i < 6; ++i) {
        if(pthread_join(chefs[i], NULL) < 0){
            fprintf(stderr,"Error in thread join\n");
            exit(-1);
        }
    }

    sem_destroy(&mutex);
    sem_destroy(&dessertMutex);
    sem_destroy(&ingredientMutex);

    printf("All the desserts have been delivered.\n");
    return 0;
}

char* ingredientName(int index){
    if(index == 0)
        return "milk";
    else if(index == 1)
        return "flour";
    else if(index == 2)
        return "walnut";
    else if(index == 3)
        return "sugar";
    else{
        return NULL;
    }
}
