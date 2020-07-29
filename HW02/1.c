#define _POSIX_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <math.h>
#include <sys/stat.h>

#define MAX 255

typedef struct pair{
    unsigned x;
    unsigned y;

} pair;

int begin = 0, fd, inputFd, outputFd, totalSignal=0, finish=0, pendingSig[20], s=0, ind=0;
static char tempFile[MAX];
double outputMSE[MAX], outputMAE[MAX], outputRMSE[MAX];

void catcher(int signum) {
    totalSignal++;
    switch (signum) {
        case SIGUSR1:
        {
            begin = 1;
            break;
        }
        case SIGTERM:
        {
            puts("SIGTERM was caught. Exitting...");
            close(inputFd);
            close(outputFd);
            close(fd);
            exit(-1);
        }
        case SIGINT:
        {
            puts("SIGINT was caught. Exitting...");
            close(inputFd);
            close(outputFd);
            close(fd);
            exit(-1);
        }
        case SIGSTOP:
        {
            puts("SIGSTOP was caught. Exiting...");
            close(inputFd);
            close(outputFd);
            close(fd);
            exit(-1);
        }
        case SIGUSR2:
        {
            puts("Parent sent a signal to the child. Child is exiting...\n");
            finish = 1;
            break;
        }
        default: printf("catcher caught unexpected signal %d\n",
                        signum);
    }
}

void pendingSignals(int signum) {
    sigset_t sigset;

    if (sigpending(&sigset) != 0)
        perror("Error sigpending");
    else if (sigismember(&sigset, signum)) {
        pendingSig[s] = signum;
        s++;
    }
}

void handler(int sig) {}

double MSE(pair pairs[], int n);
double MAE(pair pairs[], int n);
int isBinaryFile(unsigned file[], int n);
int isASCII(unsigned file[], int size);
int convertToExtAscii(unsigned file[], int size, char* filename);

int main(int argc, char *argv[]) {
    sigset_t sigset;
    struct sigaction sact;
    __pid_t pid;
    struct flock lock;
    mode_t filePerms;
    char tempFile[32];

    strcpy(tempFile, "/tmp/myfileXXXXXX");
    fd = mkstemp(tempFile);

    char buf[BUFSIZ];
    int n;

    char* inputFile = "";
    char* outputFile = "";

    int option;

    if(argc != 5){
        perror("Invalid number of arguments\n");
        return -1;
    }

    while((option = getopt(argc, argv, ":i:o:")) != -1){
        switch(option){
            case 'i':
                inputFile = optarg;
                break;
            case 'o':
                outputFile = optarg;
                break;
            case '?':
                printf("unknown option: %c\n", optopt);
                break;
        }
    }
    struct stat fileStat1, fileStat2;

    int f1 = open(inputFile, O_RDONLY);
    int f2 = open(outputFile, O_RDONLY);


    if(fstat(f1,&fileStat1) < 0) {
        perror("Fstat error\n");
        return 1;
    }

    if(fstat(f2,&fileStat2) < 0) {
        perror("Fstat error\n");
        return 1;
    }

    if(close(f1) < 0){
        perror("Error while closing file\n");
        return 1;
    }
    if(close(f2) < 0){
        perror("Error while closing file\n");
        return 1;
    }

    if(S_ISREG(fileStat1.st_mode) == 0) {
        perror("The input file should be a regular file\n");
        return 1;
    }

    if(S_ISREG(fileStat2.st_mode) == 0) {
        perror("The output file should be a regular file\n");
        return 1;
    }

    int f = open(inputFile, O_RDONLY);
    unsigned l[MAX];
    char x[1];
    int loop=0, z, i=0;

    // reading the given input file
    while ((z = read(f, x, sizeof(char))) > 0) {
        // printf("%c", (unsigned) x[0]);
        l[i] = x[0];
        i++;
    }

    if(close(f) < 0)
        perror("Error while closing the input file\n");

    if(isBinaryFile(l, i)==0 && isASCII(l,n) ==0){
        perror("The input file should be either Binary or ASCII\n");
        return 1;
    }
//    if(isBinaryFile(l, i)){
//        convertToExtAscii(l, i, inputFile);
//    }

    pid = fork();

    if (pid == 0) {
        while(finish==0) {
            sigemptyset(&sact.sa_mask);
            sact.sa_flags = 0;
            sact.sa_handler = catcher;

            sigset_t maskedSignals;
            int numRead, size = 0, openFlags;

            unsigned arr[10];

            char c[1];

            if (sigaction(SIGUSR1, &sact, NULL) != 0)
                perror("SIGUSR1 sigaction() error");

            if (sigaction(SIGINT, &sact, NULL) != 0 && errno != EINTR)
                perror("SIGINT sigaction() error");
            if (sigaction(SIGTERM, &sact, NULL) != 0  && errno != EINTR)
                perror("SIGTERM sigaction() error");

            if(sigemptyset(&sigset) != 0)
                perror("Error in sigfillset\n");

            if (sigsuspend(&sigset) == -1) {
                printf("Suspension is over. Child starts to execute.\n");
            }

            if (begin) {
                if (sigaction(SIGUSR2, &sact, NULL) != 0)
                    perror("SIGUSR1 sigaction() error");

                // CS
                if ((sigemptyset(&maskedSignals) == -1) || (sigaddset(&maskedSignals, SIGINT) == -1) ||
                    (sigaddset(&maskedSignals, SIGSTOP) == -1)) {
                    perror("Error while creating signal set to be blocked\n");
                    return -1;
                }
                // block signals
                if (sigprocmask(SIG_BLOCK, &maskedSignals, NULL) == -1) {
                    perror("Error while blocking signals\n");
                    return -1;
                }

                openFlags = O_APPEND | O_CREAT | O_WRONLY;
                filePerms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

                outputFd = open(outputFile, openFlags, filePerms);

                // temp file reading and writing
                lock.l_type = F_WRLCK;
                if (fcntl(fd, F_SETLK, &lock) < 0)
                    printf("Error while locking the input file for writing. %s\n", strerror(errno));

                lseek(fd, 0L, SEEK_SET);

                char line[MAX];
                int i=0;
                while ((numRead = read(fd, c, sizeof(char))) > 0){
                    line[i] = c[0];
                    i++;
                }

                ftruncate(fd, 0);

                if(strcat(line, ", ") == NULL)
                    perror("Error while using strcat\n");

                if (write(outputFd, line, strlen(line)) < 0){
                    perror("couldn't write whole buffer");
                }

                char *token;

                token = strtok(line, "()+,");

                pair coordinates[5];
                pair p;
                int index=0;
                while (token != NULL) {

                    if (size == 0) {
                        p.x = (unsigned) atoi(token);
                        size++;
                    } else if (size == 1) {
                        p.y = (unsigned) atoi(token);
                        coordinates[index] = p;
                        size = 0;
                        index++;
                    }
                    token = strtok(NULL, "()+,");
                }

                lock.l_type = F_UNLCK;
                if (fcntl(fd, F_SETLK, &lock) < 0)
                    perror("Error while unlocking the input temp file for writing.\n");
                // end of CS

                // unblock signals
                if (sigprocmask(SIG_UNBLOCK, &maskedSignals, NULL) == -1) {
                    perror("Error while blocking signals\n");
                    return -1;
                }

                char result[MAX];
                char t[20];
                int n = sizeof(coordinates)/sizeof(coordinates[0]);


                if(outputFd < 0){
                    perror("Error while opening the output file\n");
                }
                double total = MSE(coordinates, n);
                outputMSE[ind] = total;
                sprintf(t, "%.3lf", total);

                if(strcat(result, t) == NULL)
                    perror("Error while using strcat\n");

                if(strcat(result, ", ") == NULL)
                    perror("Error while using strcat\n");

                total = sqrt(total);
                outputRMSE[ind] = total;

                sprintf(t, "%.3f", total);

                if(strcat(result, t) == NULL)
                    perror("Error while using strcat\n");

                if(strcat(result, ", ") == NULL)
                    perror("Error while using strcat\n");

                total = MAE(coordinates, n);
                outputMAE[ind] = total;

                ind++;

                sprintf(t, "%.3lf", total);

                if(strcat(result, t) == NULL)
                    perror("Error while using strcat\n");

                if(strcat(result, "\n") == NULL)
                    perror("Error while using strcat\n");

                if(strcat(line, ", ") == NULL)
                    perror("Error while using strcat\n");

                if (write(outputFd, line, strlen(line)) < 0){
                    perror("couldn't write whole buffer");
                }
                if (write(outputFd, result, strlen(result)) < 0){
                    perror("couldn't write whole buffer");
                }
                if(memset(result,'\0', sizeof(result[0])) == NULL){
                    perror("Error in memset\n");
                }

                if(memset(line, '\0', sizeof(line[0])) == NULL){
                    perror("Error in memset\n");
                }

                if(close(outputFd) < 0){
                    perror("Error while closing the output file\n");
                }

                begin = 0;
            }
        }
        unlink(tempFile);
        if(close(fd) <0){
            perror("Error while closing the temp file\n");
        }
        if(close(inputFd) < 0){
            perror("Error while closing the input file\n");
        }
        if(close(outputFd) < 0){
            perror("Error while closing the input file\n");
        }
        double mae=0.0, mse=0.0, rmse=0.0, diff, var1=0.0, var2=0.0, var3=0.0;

        for (int j = 0; j <ind ; ++j) {
            mae += outputMAE[j];
            mse += outputMSE[j];
            rmse += outputRMSE[j];
        }
        mae /=ind;
        rmse /=ind;
        mse /=ind;

        for(i=0; i<ind; i++)
        {
            diff = outputMAE[i] - mae;
            var1 += + pow(diff,2);

            diff = outputMSE[i] - mse;
            var2 += + pow(diff,2);

            diff = outputRMSE[i] - rmse;
            var3 += + pow(diff,2);

        }
        printf("\n\n-------------------------\n");
        printf("METRIC\tMEAN\tSTANDARD DEVIATION\n");
        printf("MAE\t%lf\t%lf\n", mae, var1);
        printf("MSE\t%lf\t%lf\n", mse, var2);
        printf("RMSE\t%lf\t%lf\n", rmse, var3);
        printf("-------------------------\n");

        exit(0);
    }
    else if (pid > 0){
        int done = 0, totalBytesRead=0, totalEquation=0;

        while(done == 0) {
            sigemptyset(&sact.sa_mask);
            sact.sa_flags = 0;
            sact.sa_handler = catcher;
            sigset_t maskedSignals;
            int numRead, size = 0, index = 0;
            pair coordinates[10];
            unsigned char c[1];
            pair p;

            if (sigaction(SIGINT, &sact, NULL) != 0 && errno != EINTR)
                perror("SIGINT sigaction() error");
            if (sigaction(SIGTERM, &sact, NULL) != 0  && errno != EINTR)
                perror("SIGTERM sigaction() error");

            inputFd = open(inputFile, O_RDONLY);
            unsigned line[MAX];
            int i = 0, loop=0;

            // reading the given input file
            while ((numRead = read(inputFd, c, sizeof(char))) > 0) {
                totalBytesRead += numRead;
                if (loop > 9) {
                    int k=0;
                    while (k<10) {
                        if (size == 0) {
                            p.x = line[k];
                            size++;
                        } else if (size == 1) {
                            p.y = line[k];
                            coordinates[index] = p;
                            size = 0;
                            index++;
                        }
                        k++;
                    }

                    index = 0;

                    // CS
                    if ((sigemptyset(&maskedSignals) == -1) || (sigaddset(&maskedSignals, SIGINT) == -1) ||
                        (sigaddset(&maskedSignals, SIGSTOP) == -1)) {
                        perror("Error while creating signal set to be blocked\n");
                        return -1;
                    }

                    // block signals
                    if (sigprocmask(SIG_BLOCK, &maskedSignals, NULL) == -1) {
                        perror("Error while blocking signals\n");
                        return -1;
                    }

                    float x[100], y[100], sumx = 0, sumx2 = 0, sumy = 0, sumyx = 0, b, a, num = 5;
                    char result[MAX];

                    for (int i = 0; i < num; i++) {
                        sumx = sumx + coordinates[i].x;
                        sumx2 = sumx2 + coordinates[i].x * coordinates[i].x;
                        sumy = sumy + coordinates[i].y;
                        sumyx = sumyx + coordinates[i].y * coordinates[i].x;
                    }

                    b = (sumx2 * sumy - sumyx * sumx) / (num * sumx2 - sumx * sumx);
                    a = (num * sumyx - sumx * sumy) / (num * sumx2 - sumx * sumx);

                    char arr[32];

                    for (int j = 0; j < num; ++j) {
                        sprintf(arr, "(%d, %d), ", coordinates[j].x, coordinates[j].y);
                        if(strcat(result, arr) == NULL)
                            perror("Error while using strcat\n");
                    }

                    sprintf(arr, "(%.3fx + %.3f)\n", a, b);
                    totalEquation++;

                    if(strcat(result, arr) == NULL)
                        perror("Error while using strcat\n");

                    pendingSignals(SIGINT);
                    pendingSignals(SIGSTOP);

                    // unblock signals
                    if (sigprocmask(SIG_UNBLOCK, &maskedSignals, NULL) == -1) {
                        perror("Error while blocking signals\n");
                        return -1;
                    }

                    if (fd < 1) {
                        printf("\n Creation of temp file failed with error [%s]\n", strerror(errno));
                        return 1;
                    }
                    else {

                        // temp file locking
                        lock.l_type = F_WRLCK;
                        if (fcntl(fd, F_SETLK, &lock) < 0)
                            printf("Error while locking the output temp file for writing. %s\n", strerror(errno));

                        // temp file writing
                        if (write(fd, result, strlen(result)) < 0)
                            perror("couldn't write the whole buffer");

//                                temp file unlocking
                        lock.l_type = F_UNLCK;
                        if (fcntl(fd, F_SETLK, &lock) < 0)
                            perror("Error while unlocking the output temp file for writing.\n");

                        if(memset(result, '\0', sizeof(result[0])) == NULL){
                            perror("Error in memset\n");
                        }
                    }

                    if(kill(pid, SIGUSR1) != 0)
                        perror("Error while sending signal to the child\n");

                    i = 0;
                    loop=0;
                } else {
                    line[i] = (unsigned) c[0];
                    i++;
                    loop++;
                }
            }

            if(numRead < 1) {
                done = 1;
                printf("\n*********************************\n");
                printf("Parent process is done.\n");
                printf("Number of bytes it has read is %d\n", totalBytesRead);
                printf("Number of line equations it has estimated is %d\n", totalEquation);
                printf("Signals where sent to P1 while it was in a critical section (if any): ");

                for (int j = 0; pendingSig[j] != '\0'; ++j) {
                    printf("%d ", pendingSig[j]);
                }
                printf("\n");

                if(kill(pid, SIGUSR2) != 0)
                    perror("Error in signalling to the child process.\n");

                unlink(tempFile);
                if(close(fd) <0){
                    perror("Error while closing the temp file\n");
                }
                if(close(inputFd) < 0){
                    perror("Error while closing the input file\n");
                }
                if(close(outputFd) < 0){
                    perror("Error while closing the input file\n");
                }
            }
        }
    }
    else{
        printf("Error while forking\n");
        return 1;
    }

    return 0;
}

double MSE(pair pairs[], int n){
    double total = 0.0;
    for (int i = 0; i < n; ++i) {
        total += pow((pairs[i].x-pairs[i].y), 2);
    }
    total /= pow(n, 2);
    return total;
}

double MAE(pair pairs[], int n){
    double total = 0.0;
    for (int i = 0; i < n; ++i) {
        total += pairs[i].y-pairs[i].x;
    }
    total /= n;
    return total;
}

int isBinaryFile(unsigned file[], int n){
    int i=0;
    while(i<n){
        if(!(file[i] == 48 || file[i] == 49 || file[i] == 32))
            return 0;
        i++;
    }

    return 1;
}

int isASCII(unsigned file[], int size){
    char t[8];
    int j=0;
    for (int i = 0; i < size; i++) {
       if(file[i] < 0 || file[i]>255){
           printf("%d ", file[i]);
           return 0;
       }
    }
    return 1;
}
// https://www.thecrazyprogrammer.com/2013/02/c-program-to-convert-given-binary.html
int convertToExtAscii(unsigned file[], int size, char* filename){

    int openFlags = O_WRONLY;
    int filePerms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    char buffer[MAX];
    int f = open(filename, openFlags, filePerms);

    unsigned char t[8];
    int j=0;
    for (int i = 0; i < size; i++) {
        if(j == 8) {
            int n = atoi(t);
            printf("%s ", t);
            int l,x=0,a;
            for (l = 0; n > 0; l += 1) {
                a = n % 10;
                x = a*(pow(2, l)) + x;
                n = n / 10;
            }
            sprintf(buffer, "%d ", x);

            write(f, buffer, strlen(buffer));

            j=0;
            i=i+1;
        }
        t[j] = (unsigned char)file[i];
        j++;
    }


    close(f);
}