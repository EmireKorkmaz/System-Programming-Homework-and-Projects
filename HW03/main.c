/*
    System Programming HW03
    Emire Korkmaz

    SVD code is taken from https://github.com/architshukla/EEG-Based-BCI/tree/master/code/svd
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/resource.h>

#define SIZE 255
struct rlimit limit;

#define PRECISION1 32768
#define PRECISION2 16384
#define MIN(x,y) ( (x) < (y) ? (x) : (y) )
#define MAX(x,y) ((x)>(y)?(x):(y))
#define SIGN(a, b) ((b) >= 0.0 ? fabs(a) : -fabs(a))
#define MAXINT 2147483647
#define minimumValue 0.5
#define ASCII_TEXT_BORDER_WIDTH 4
#define MAXHIST 100
#define STEP0 0.01
#define FORWARD 1
#define BACKWARD -1
#define PROJ_DIM 5
#define True 1
#define False 0
#define stackSize getrlimit (RLIMIT_STACK, &limit)
#define upperLimit (limit.rlim_cur)

typedef struct {
    float x, y, z;
} fcoords;

typedef struct {
    long x, y, z;
} lcoords;

typedef struct {
    int x, y, z;
} icoords;

typedef struct {
    float min, max;
} lims;

/* grand tour history */
typedef struct hist_rec {
  struct hist_rec *prev, *next;
  float *basis[3];
  int pos;
} hist_rec;


__pid_t children[4], parent;
int done = 0, fd1, fd2, pipe1[2], pipe2[2], pipe3[2], pipe4[2], n;
int findIndex(__pid_t pid);

static double PYTHAG(double a, double b);
int dsvd(float **a, int m, int n, float *w, float **v);
void swapColumns(float **u, int i, int j, int row, int col);
void displayMatrix(float **mat, int row, int col);
void sortSigmaValues(float **u, float *w, float **v, int row, int col);
void PCA(float *w, int col);

void catcher(int signum) {
    pid_t pid;
    int stat; 
    int status;
    switch (signum) {
        case SIGCHLD: {
            while((pid = waitpid(-1, &status, WNOHANG)) > 0){
                done++;
            }
            if(done > 4){
                kill(parent, SIGUSR2);
            }
            break;
        }
        case SIGUSR1:{
            for (int i = 0; i < 4; ++i){
                kill(children[i], SIGUSR2);
            }
            break;
        }
        case SIGUSR2:{
            break;
        }
        case SIGINT:{
            puts("sigint has been caught\n");
            if((fcntl(fd1, F_GETFD) != -1) && (close(fd1) < 0)){
                 perror("Error while closing the first input file\n");
            }

             if((fcntl(fd2, F_GETFD) != -1) && (close(fd2) < 0)){
                 perror("Error while closing the second input file\n");
             }
            exit(EXIT_SUCCESS);
            break;
        }
        default:{
            puts("default signal\n");
            break;
        }
    }
}

int main(int argc, char *argv[]) {

    __pid_t pid1, pid2, pid3, pid4;

    char* inputFile1 = "";
    char* inputFile2 = "";

    getrlimit (RLIMIT_STACK, &limit);

    struct sigaction sact;

    sigemptyset(&sact.sa_mask);
    sact.sa_flags = 0;
    sact.sa_handler = catcher;

    if (sigaction(SIGCHLD, &sact, NULL) != 0)
        perror("SIGCHLD sigaction() error");
    if (sigaction(SIGUSR1, &sact, NULL) != 0)
        perror("SIGUSR1 sigaction() error");
    if (sigaction(SIGUSR2, &sact, NULL) != 0)
        perror("SIGUSR2 sigaction() error");
    if (sigaction(SIGINT, &sact, NULL) != 0)
        perror("SIGINT sigaction() error");


    if(argc != 7){
        perror("Invalid number of arguments\n");
        return -1;
    }

    int option;

    while((option = getopt(argc, argv, ":i:j:n:")) != -1){
        switch(option){
            case 'i':
                inputFile1 = optarg;
                break;
            case 'j':
                inputFile2 = optarg;
                break;
            case 'n':
                n = atoi(optarg);
                break;
            case '?':
                printf("unknown option: %c\n", optopt);
                break;
        }
    }

    if(n<1){
        perror("n should be a positive number\n");
        return -1;
    }

    float stackLimit = sqrt(upperLimit)/((int) pow(2, n)*(int) pow(2,n));

    if(stackLimit < minimumValue){
        fprintf (stderr,"Stack limit is not enough\n");
        exit(-1);
    }

    if((pipe(pipe1) == -1) || (pipe(pipe2) == -1) || (pipe(pipe3) == -1) || (pipe(pipe4) == -1)){
        perror("Error while creating the pipes\n");
        return -1;
    }

    struct stat st1;
    if(stat(inputFile1, &st1) < 0){
        fprintf(stderr, "Error stat\n");
        exit(-1);
    }

    int size1 = st1.st_size;

    struct stat st2;

    if(stat(inputFile2, &st2) < 0){
        fprintf(stderr, "Error stat\n");
        exit(-1);
    }
    int size2 = st2.st_size;

    if(size1==0 || size2 ==0){
        fprintf(stderr, "The given files should not be empty\n");
        exit(-1);
    }

    parent = getpid();

    for(int i=0 ;i<4;i++)
    {
        if(fork() == 0)
        {
            children[i] = getpid();
            break;
        }
    }

    if(getpid() == parent){

         fd1 = open(inputFile1, O_RDONLY);
         fd2 = open(inputFile2, O_RDONLY);

         if(fd1 < 0){
             perror("Error while opening the first input file\n");
             return -1;
         }

         if(fd2 < 0){
             perror("Error while opening the second input file\n");
             return -1;
         }

        int numRead, total=0, i=0, k=0, l=0;
        char c, line[SIZE];
        int C[SIZE][SIZE],child1[SIZE][SIZE], child2[SIZE][SIZE], child3[SIZE][SIZE], child4[SIZE][SIZE], firstMatrix[SIZE][SIZE], secondMatrix[SIZE][SIZE], temp[SIZE][SIZE];

        while ((numRead = read(fd1, &c, sizeof(char))) > 0) {
             
            if(i == (int) pow(2,n)-1){
                line[i]=(int)c;
                for (int l = 0; l <  (int) pow(2,n); ++l)
                    firstMatrix[k][l] = line[l];

                k++;
                i=0;
                memset(line, '\0', sizeof(line));
            }
            else{
                line[i]=(int)c;
                i++;

            }
        }

        i=0, k=0, l=0;
        memset(line, '\0', sizeof(line));
        while ((numRead = read(fd2, &c, sizeof(char))) > 0) {

            if(i == (int) pow(2,n)-1){
                line[i]=(int)c;
                for (int l = 0; l <  (int) pow(2,n); ++l)
                    temp[k][l] = line[l];

                k++;
                i=0;
                memset(line, '\0', sizeof(line));
            }
            else{
                line[i]=(int)c;
                i++;

            }
        }
        printf("Matrix A:\n");
        for (int i = 0; i < (int) pow(2,n); ++i){
            for (int j = 0; j < (int) pow(2,n); ++j){
                printf("%d ", firstMatrix[i][j]);
            }
            printf("\n");
        }
        
        printf("\nMatrix B:\n");

        for (int i = 0; i < (int) pow(2,n); ++i){
            for (int j = 0; j < (int) pow(2,n); ++j){
                printf("%d ", temp[i][j]);
            }
            printf("\n");
        }

        int quarter1 = (int) pow(2, n)/4, quarter2 = quarter1*2, quarter3 = quarter1*3;

        for (int i = 0; i < (int) pow(2, n); i++) 
            for (int j = 0; j < (int) pow(2, n); j++) 
                secondMatrix[i][j] = temp[j][i]; 

        for (int i = 0; i < quarter1; ++i)
            for (int j = 0; j < (int) pow(2, n); ++j){
                child1[i][j] = secondMatrix[i][j];
            }

        for (int i = 0, l=quarter1; i < quarter1; ++i, l++)
            for (int j = 0; j < (int) pow(2, n); ++j)
                child2[i][j] = secondMatrix[l][j];

        for (int i = 0, l=quarter2; i < quarter1; ++i, l++)
            for (int j = 0; j < (int) pow(2, n); ++j)
                child3[i][j] = secondMatrix[l][j];
 
         for (int i = 0, l=quarter3; i < quarter1; ++i, l++)
            for (int j = 0; j < (int) pow(2, n); ++j)
                child4[i][j] = secondMatrix[l][j];      

         if(close(fd1) < 0){
             perror("Error while closing the first input file\n");
             return -1;
         }

         if(close(fd2) < 0){
             perror("Error while closing the second input file\n");
             return -1;
         }

        // 1st child
        for (int i = 0; i < quarter1; ++i)
            if (write(pipe1[1], &child1[i], sizeof(int)* (int) pow(2, n)) < 0){
                perror("Error writing pipe first time\n");
                exit(-1);
            }

        for (int l = 0; l < (int) pow(2, n); ++l) {
            if (write(pipe1[1], &firstMatrix[l], sizeof(int) * (int) pow(2, n)) < 0){
                perror("Error  writing pipe first time\n");
                exit(-1);
            }
        }

        // 2nd child
        for (int i = 0; i < quarter1; ++i)
            if (write(pipe2[1], &child2[i], sizeof(int) * (int) pow(2, n)) < 0){
                perror("Error writing pipe first time\n");
                exit(-1);
            }

        for (int l = 0; l < (int) pow(2, n); ++l) {
            if (write(pipe2[1], &firstMatrix[l], sizeof(int) * (int) pow(2, n)) < 0){
                perror("Error  writing pipe first time\n");
                exit(-1);

            }
        }

        //3rd child
        for (int i = 0; i < quarter1; ++i)
            if (write(pipe3[1], &child3[i], sizeof(int) * (int) pow(2, n)) < 0){
                perror("Error writing pipe first time\n");
                exit(-1);
            }

        for (int l = 0; l < (int) pow(2, n); ++l) {
            if (write(pipe3[1], &firstMatrix[l], sizeof(int) * (int) pow(2, n)) < 0){
                perror("Error  writing pipe first time\n");
                exit(-1);

            }
        }

        // 4th child
        for (int i = 0; i < quarter1; ++i)
            if (write(pipe4[1], &child4[i], sizeof(int) * (int) pow(2, n)) < 0){
                perror("Error writing pipe first time\n");
                exit(-1);
            }

        for (int l = 0; l < (int) pow(2, n); ++l) {
            if (write(pipe4[1], &firstMatrix[l], sizeof(int) * (int) pow(2, n)) < 0){
                perror("Error  writing pipe first time\n");
                exit(-1);

            }
        }

        if(kill(getpid(), SIGUSR1) < 0){
            fprintf(stderr, "Error kill\n");
            exit(-1);
        }

        pid_t child_pid, wpid;
        int status = 0;
        while ((wpid = wait(&status)) > 0);

        int buf1[SIZE][SIZE];

        for (int i = 0; i < ((int) pow(2, n))/4; ++i)
            if (read(pipe1[0], &buf1[i], sizeof(int)* (int) pow(2, n)) < 0)
                perror("Error reading pipe1\n");


        int buf2[SIZE][SIZE];
        for (int i = 0; i < ((int) pow(2, n))/4; ++i)
            if (read(pipe2[0], &buf2[i], sizeof(int)* (int) pow(2, n)) < 0)
                perror("Error reading pipe2\n");


        int buf3[SIZE][SIZE];
        for (int i = 0; i < ((int) pow(2, n))/4; ++i)
            if (read(pipe3[0], &buf3[i], sizeof(int)* (int) pow(2, n)) < 0)
                perror("Error reading pipe3\n");


        int buf4[SIZE][SIZE];

        for (int i = 0; i < ((int) pow(2, n))/4; ++i)
            if (read(pipe4[0], &buf4[i], sizeof(int)* (int) pow(2, n)) < 0)
                perror("Error reading pipe4\n");

        if((close(pipe1[0]) < 0) || (close(pipe1[1]) < 0) || (close(pipe2[0]) < 0) || (close(pipe2[1]) < 0) || (close(pipe3[0]) < 0)
            || (close(pipe3[1]) < 0) || (close(pipe4[0]) < 0) || (close(pipe4[1]) < 0)){
            perror("Error while closing the pipes\n");
        }

        float **a = (float**) malloc((int)pow(2,n) * sizeof(float*));

        for(int i=0;i<(int)pow(2,n);i++)
        {
            *(a+i) = (float*) malloc((int)pow(2,n) * sizeof(float));
        }

        for (int i = 0; i < quarter1; ++i)
            for (int j = 0; j < (int) pow(2,n); ++j){
                temp[i][j] = buf1[i][j];
                a[i][j] = (float)buf1[i][j];
            }

        for (int i = quarter1,l=0; i < quarter2; ++i,l++)
            for (int j = 0; j < (int) pow(2,n); ++j){
                temp[i][j] = buf2[l][j];
                a[i][j] = (float)buf2[l][j];
            }

        for (int i = quarter2,l=0; i < quarter3; ++i,l++)
            for (int j = 0; j < (int) pow(2,n); ++j){
                temp[i][j] = buf3[l][j];
                a[i][j] = (float)buf3[l][j];
            }

        for (int i = quarter3,l=0; i < (int) pow(2,n); ++i,l++)
            for (int j = 0; j < (int) pow(2,n); ++j){
                temp[i][j] = buf4[l][j];
                a[i][j] = (float)buf4[l][j];
            }

        printf("\nMatrix C:\n");

        for (int i = 0; i < (int) pow(2, n); i++) 
            for (int j = 0; j < (int) pow(2, n); j++) 
                C[i][j] = temp[j][i]; 

        for (int i = 0; i < (int) pow(2,n); ++i){
            for (int j = 0; j < (int) pow(2,n); ++j)
                printf("%d ",C[i][j]);
            printf("\n");
        }

        printf("\nMatrix C (Float):\n");

        for (int i = 0; i < (int) pow(2,n); ++i){
            for (int j = 0; j < (int) pow(2,n); ++j)
                printf("%.3f ",a[i][j]);
            printf("\n");
        }

        int j=0;

        float **v = (float**) malloc((int) pow(2,n) * sizeof(float*));
        for(int i=0;i<(int) pow(2,n);i++)
        {
            *(v+i) = (float*) malloc((int) pow(2,n) * sizeof(float));
        }
        float *w = (float*) malloc((int) pow(2,n) * sizeof(float));

        dsvd(a, (int) pow(2,n), (int) pow(2,n), w, v);

        printf("\nSingular Values of C: \n");

        for (int i = 0; i < (int)pow(2,n); ++i)
            printf("%.3f ", w[i]);

        printf("\n");

        for(i=0;i<(int) pow(2,n);i++)
        {
            free(a[i]);
            free(v[i]);
        }


        free(a);
        free(v);
        free(w);

        exit(EXIT_SUCCESS);
    }
    else if(getpid() == children[0]){
        children[0] = getpid();
        sigset_t sigmask;
        
        if(sigemptyset(&sigmask) < 0){
            fprintf(stderr, "Error sigemptyset\n");
            exit(-1);
        }

        while (sigsuspend(&sigmask) != -1);

        int buf[SIZE][SIZE], matrix[SIZE][SIZE], result[SIZE][SIZE];
        
        for (int l = 0; l < (int) pow(2, n)/4; ++l) {
            if(read(pipe1[0], buf[l], sizeof(int)*(int) pow(2, n)) <0)
                perror("read error\n");
        }

        for (int l = 0; l < (int) pow(2, n); ++l) {
            if (read(pipe1[0], matrix[l], sizeof(int) * (int) pow(2, n)) < 0)
                perror("Error reading matrix pipe\n");
        }

        for(int i=0; i<(int)pow(2,n)/4; i++){
            for (int j = 0; j < (int)pow(2,n); ++j) {
                result[i][j]=0;
            }
        }
        
        int sum=0, c,d,k;
        for (c = 0; c < (int) pow(2, n)/4; c++) {
            for (d = 0; d < (int) pow(2, n); d++) {
                for (k = 0; k < (int) pow(2, n); k++) {
                    sum += matrix[d][k]*buf[c][k];
                }
                result[c][d] = sum;
                sum = 0;
            }
        }

        for (int i = 0; i < (int) pow(2, n)/4; ++i){
            if (write(pipe1[1], &result[i], sizeof(int) * (int) pow(2, n)) < 0)
                perror("Error writing pipe\n");
        }

        _exit(EXIT_SUCCESS);

    }
    else if(getpid() == children[1]){
        children[1] = getpid();
        sigset_t sigmask;
        
        if(sigemptyset(&sigmask) < 0){
            fprintf(stderr, "Error sigemptyset\n");
            exit(-1);
        }

        while (sigsuspend(&sigmask) != -1);

        int buf[SIZE][SIZE], matrix[SIZE][SIZE], result[SIZE][SIZE];

        for (int l = 0; l < (int) pow(2, n)/4; ++l) {
            if(read(pipe2[0], buf[l], sizeof(int)*(int) pow(2, n)) <0)
                perror("read error\n");
        }

        for (int l = 0; l < (int) pow(2, n); ++l) {
            if (read(pipe2[0], matrix[l], sizeof(int) * (int) pow(2, n)) < 0)
                perror("Error reading matrix pipe\n");
        }

        for(int i=0; i<(int)pow(2,n)/4; i++){
            for (int j = 0; j < (int)pow(2,n); ++j){
                result[i][j]=0;
            }
        }

        int sum=0, c,d,k;
        for (c = 0; c < (int) pow(2, n)/4; c++) {
            for (d = 0; d < (int) pow(2, n); d++) {
                for (k = 0; k < (int) pow(2, n); k++) {
                    sum += matrix[d][k]*buf[c][k];
                    
                }

                result[c][d] = sum;
                sum = 0;
            }
        }

        for (int i = 0; i < (int) pow(2, n)/4; ++i){
            if (write(pipe2[1], &result[i], sizeof(int) * (int) pow(2, n)) < 0)
                perror("Error writing pipe\n");
        }
        _exit(EXIT_SUCCESS);

    }
    else if(getpid()== children[2]){
        children[2] = getpid();

        sigset_t sigmask;

        if(sigemptyset(&sigmask) < 0){
            fprintf(stderr, "Error sigemptyset\n");
            exit(-1);
        }

        while (sigsuspend(&sigmask) != -1);

        int buf[SIZE][SIZE], matrix[SIZE][SIZE], result[SIZE][SIZE];
        
        for (int l = 0; l < (int) pow(2, n)/4; ++l) {
            if(read(pipe3[0], buf[l], sizeof(int)*(int) pow(2, n)) <0)
                perror("read error\n");
        }

        for (int l = 0; l < (int) pow(2, n); ++l) {
            if (read(pipe3[0], matrix[l], sizeof(int) * (int) pow(2, n)) < 0)
                perror("Error reading matrix pipe\n");
        }

        for(int i=0; i<(int)pow(2,n)/4; i++){
            for (int j = 0; j < (int)pow(2,n); ++j){
                result[i][j]=0;
            }
        }        

        int sum=0, c, d, k;
        for (c = 0; c < (int) pow(2, n)/4; c++) {
            for (d = 0; d < (int) pow(2, n); d++) {
                for (k = 0; k < (int) pow(2, n); k++) {
                    sum += matrix[d][k]*buf[c][k];
                }
                result[c][d] = sum;
                sum = 0;
            }
        }


        for (int i = 0; i < (int) pow(2, n)/4; ++i){
            if (write(pipe3[1], &result[i], sizeof(int) * (int) pow(2, n)) < 0)
                perror("Error writing pipe\n");
        }

        _exit(EXIT_SUCCESS);
    }
    else if(getpid()== children[3]){
        children[3] = getpid();

        sigset_t sigmask;

        if(sigemptyset(&sigmask) < 0){
            fprintf(stderr, "Error sigemptyset\n");
            exit(-1);
        }

        while (sigsuspend(&sigmask) != -1);

        int buf[SIZE][SIZE], matrix[SIZE][SIZE], result[SIZE][SIZE];
        
        for (int l = 0; l < (int) pow(2, n)/4; ++l) {
            if(read(pipe4[0], buf[l], sizeof(int)*(int) pow(2, n)) <0)
                perror("read error\n");
        }

        for (int l = 0; l < (int) pow(2, n); ++l) {
            if (read(pipe4[0], matrix[l], sizeof(int) * (int) pow(2, n)) < 0)
                perror("Error reading matrix pipe\n");
        }

        for(int i=0; i<(int)pow(2,n)/4; i++){
            for (int j = 0; j < (int)pow(2,n); ++j){
                result[i][j]=0;
            }
        }

        int sum=0, c,d,k;
        for (c = 0; c < (int) pow(2, n)/4; c++) {
            for (d = 0; d < (int) pow(2, n); d++) {
                for (k = 0; k < (int) pow(2, n); k++) {
                    sum += matrix[d][k]*buf[c][k];
                }
                result[c][d] = sum;
                sum = 0;
            }
        }

        for (int i = 0; i < (int) pow(2, n)/4; ++i){
            if (write(pipe4[1], &result[i], sizeof(int) * (int) pow(2, n)) < 0)
                perror("Error writing pipe\n");
        }

        _exit(EXIT_SUCCESS);

    }
    return 0;
}

 
static double PYTHAG(double a, double b)
{
    double at = fabs(a), bt = fabs(b), ct, result;

    if (at > bt)       { ct = bt / at; result = at * sqrt(1.0 + ct * ct); }
    else if (bt > 0.0) { ct = at / bt; result = bt * sqrt(1.0 + ct * ct); }
    else result = 0.0;
    return(result);
}


int dsvd(float **a, int m, int n, float *w, float **v)
{
    int flag, i, its, j, jj, k, l, nm;
    double c, f, h, s, x, y, z;
    double anorm = 0.0, g = 0.0, scale = 0.0;
    double *rv1;
  
    if (m < n) 
    {
        fprintf(stderr, "#rows must be > #cols \n");
        return(0);
    }
  
    rv1 = (double *)malloc((unsigned int) n*sizeof(double));

/* Householder reduction to bidiagonal form */
    for (i = 0; i < n; i++) 
    {
        /* left-hand reduction */
        l = i + 1;
        rv1[i] = scale * g;
        g = s = scale = 0.0;
        if (i < m) 
        {
            for (k = i; k < m; k++) 
                scale += fabs((double)a[k][i]);
            if (scale) 
            {
                for (k = i; k < m; k++) 
                {
                    a[k][i] = (float)((double)a[k][i]/scale);
                    s += ((double)a[k][i] * (double)a[k][i]);
                }
                f = (double)a[i][i];
                g = -SIGN(sqrt(s), f);
                h = f * g - s;
                a[i][i] = (float)(f - g);
                if (i != n - 1) 
                {
                    for (j = l; j < n; j++) 
                    {
                        for (s = 0.0, k = i; k < m; k++) 
                            s += ((double)a[k][i] * (double)a[k][j]);
                        f = s / h;
                        for (k = i; k < m; k++) 
                            a[k][j] += (float)(f * (double)a[k][i]);
                    }
                }
                for (k = i; k < m; k++) 
                    a[k][i] = (float)((double)a[k][i]*scale);
            }
        }
        w[i] = (float)(scale * g);
    
        /* right-hand reduction */
        g = s = scale = 0.0;
        if (i < m && i != n - 1) 
        {
            for (k = l; k < n; k++) 
                scale += fabs((double)a[i][k]);
            if (scale) 
            {
                for (k = l; k < n; k++) 
                {
                    a[i][k] = (float)((double)a[i][k]/scale);
                    s += ((double)a[i][k] * (double)a[i][k]);
                }
                f = (double)a[i][l];
                g = -SIGN(sqrt(s), f);
                h = f * g - s;
                a[i][l] = (float)(f - g);
                for (k = l; k < n; k++) 
                    rv1[k] = (double)a[i][k] / h;
                if (i != m - 1) 
                {
                    for (j = l; j < m; j++) 
                    {
                        for (s = 0.0, k = l; k < n; k++) 
                            s += ((double)a[j][k] * (double)a[i][k]);
                        for (k = l; k < n; k++) 
                            a[j][k] += (float)(s * rv1[k]);
                    }
                }
                for (k = l; k < n; k++) 
                    a[i][k] = (float)((double)a[i][k]*scale);
            }
        }
        anorm = MAX(anorm, (fabs((double)w[i]) + fabs(rv1[i])));
    }
  
    /* accumulate the right-hand transformation */
    for (i = n - 1; i >= 0; i--) 
    {
        if (i < n - 1) 
        {
            if (g) 
            {
                for (j = l; j < n; j++)
                    v[j][i] = (float)(((double)a[i][j] / (double)a[i][l]) / g);
                    /* double division to avoid underflow */
                for (j = l; j < n; j++) 
                {
                    for (s = 0.0, k = l; k < n; k++) 
                        s += ((double)a[i][k] * (double)v[k][j]);
                    for (k = l; k < n; k++) 
                        v[k][j] += (float)(s * (double)v[k][i]);
                }
            }
            for (j = l; j < n; j++) 
                v[i][j] = v[j][i] = 0.0;
        }
        v[i][i] = 1.0;
        g = rv1[i];
        l = i;
    }
  
    /* accumulate the left-hand transformation */
    for (i = n - 1; i >= 0; i--) 
    {
        l = i + 1;
        g = (double)w[i];
        if (i < n - 1) 
            for (j = l; j < n; j++) 
                a[i][j] = 0.0;
        if (g) 
        {
            g = 1.0 / g;
            if (i != n - 1) 
            {
                for (j = l; j < n; j++) 
                {
                    for (s = 0.0, k = l; k < m; k++) 
                        s += ((double)a[k][i] * (double)a[k][j]);
                    f = (s / (double)a[i][i]) * g;
                    for (k = i; k < m; k++) 
                        a[k][j] += (float)(f * (double)a[k][i]);
                }
            }
            for (j = i; j < m; j++) 
                a[j][i] = (float)((double)a[j][i]*g);
        }
        else 
        {
            for (j = i; j < m; j++) 
                a[j][i] = 0.0;
        }
        ++a[i][i];
    }

    /* diagonalize the bidiagonal form */
    for (k = n - 1; k >= 0; k--) 
    {                             /* loop over singular values */
        for (its = 0; its < 30; its++) 
        {                         /* loop over allowed iterations */
            flag = 1;
            for (l = k; l >= 0; l--) 
            {                     /* test for splitting */
                nm = l - 1;
                if (fabs(rv1[l]) + anorm == anorm) 
                {
                    flag = 0;
                    break;
                }
                if (fabs((double)w[nm]) + anorm == anorm) 
                    break;
            }
            if (flag) 
            {
                c = 0.0;
                s = 1.0;
                for (i = l; i <= k; i++) 
                {
                    f = s * rv1[i];
                    if (fabs(f) + anorm != anorm) 
                    {
                        g = (double)w[i];
                        h = PYTHAG(f, g);
                        w[i] = (float)h; 
                        h = 1.0 / h;
                        c = g * h;
                        s = (- f * h);
                        for (j = 0; j < m; j++) 
                        {
                            y = (double)a[j][nm];
                            z = (double)a[j][i];
                            a[j][nm] = (float)(y * c + z * s);
                            a[j][i] = (float)(z * c - y * s);
                        }
                    }
                }
            }
            z = (double)w[k];
            if (l == k) 
            {                  /* convergence */
                if (z < 0.0) 
                {              /* make singular value nonnegative */
                    w[k] = (float)(-z);
                    for (j = 0; j < n; j++) 
                        v[j][k] = (-v[j][k]);
                }
                break;
            }
            if (its >= 30) {
                free((void*) rv1);
                fprintf(stderr, "No convergence after 30,000! iterations \n");
                return(0);
            }
    
            /* shift from bottom 2 x 2 minor */
            x = (double)w[l];
            nm = k - 1;
            y = (double)w[nm];
            g = rv1[nm];
            h = rv1[k];
            f = ((y - z) * (y + z) + (g - h) * (g + h)) / (2.0 * h * y);
            g = PYTHAG(f, 1.0);
            f = ((x - z) * (x + z) + h * ((y / (f + SIGN(g, f))) - h)) / x;
          
            /* next QR transformation */
            c = s = 1.0;
            for (j = l; j <= nm; j++) 
            {
                i = j + 1;
                g = rv1[i];
                y = (double)w[i];
                h = s * g;
                g = c * g;
                z = PYTHAG(f, h);
                rv1[j] = z;
                c = f / z;
                s = h / z;
                f = x * c + g * s;
                g = g * c - x * s;
                h = y * s;
                y = y * c;
                for (jj = 0; jj < n; jj++) 
                {
                    x = (double)v[jj][j];
                    z = (double)v[jj][i];
                    v[jj][j] = (float)(x * c + z * s);
                    v[jj][i] = (float)(z * c - x * s);
                }
                z = PYTHAG(f, h);
                w[j] = (float)z;
                if (z) 
                {
                    z = 1.0 / z;
                    c = f * z;
                    s = h * z;
                }
                f = (c * g) + (s * y);
                x = (c * y) - (s * g);
                for (jj = 0; jj < m; jj++) 
                {
                    y = (double)a[jj][j];
                    z = (double)a[jj][i];
                    a[jj][j] = (float)(y * c + z * s);
                    a[jj][i] = (float)(z * c - y * s);
                }
            }
            rv1[l] = 0.0;
            rv1[k] = f;
            w[k] = (float)x;
        }
    }
    free((void*) rv1);
    return(1);
}

void PCA(float *w, int col)
{
    /* Function to perform PCA
       Input:
    w: The vector of sigma values
    col: number of elements in the vector */
    int i;
    float sum = 0, sum99Percent, currentSum = 0;
    for(i = 0; i < col; i++)
    {
        sum += w[i];
    }
    
    sum99Percent = sum * 99.0/100.0;
    
    for(i = 0;i < col; i++)
    {
        if(currentSum >= sum99Percent)
            w[i]=0;
        currentSum += w[i];
    }
}

void swapColumns(float **u, int i, int j, int row, int col)
{
    /* Function to swap two columns of a matrix
       Input:
        u: matrix whose columns are to be swapped
        i: index of first column
        j: index of second column
        row: number of rows in the matrix
        col: number of columns in the matrix */
    int index;
    for(index=0;index<row;index++)
    {
        float temp = u[index][i];
        u[index][i] = u[index][j];
        u[index][j] = temp;
    }
}

void sortSigmaValues(float **u, float *w, float **v, int row, int col)
{
    /* Function to sort the sigma values (and correspondingly sort other matrices) in decreasing order
       Input:
        u, w, v : the resultant matrices from SVD
        row: number of rows of u
        col: number of columns of u 
       Uses:
        - swapColumns() */
    int i,j;
    for(i = 0; i < col; i++)
    {
        for(j = i + 1; j < col; j++)
        {
            if(w[i] < w[j])
            {
                float temp = w[i];
                w[i] = w[j];
                w[j] = temp;
                swapColumns(u, i, j, row, col);
                swapColumns(v, i, j, col, col);
            }
        }
    }
}

void displayMatrix(float **mat, int row, int col)
{
    /* Function to display a row x col matrix
       Input: 
        mat: matrix to be displayed
        row: number of rows
        col: number of columns */
    int i,j;
    for(i = 0; i < row; i++)
    {
        for(j = 0; j < col; j++)
        {
            printf("%f ",mat[i][j]);
        }
        printf("\n");
    }
}