#include <stdio.h>  
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>

#define NUM_OF_BYTES 32

char* convert(int arr[], char* str);

int main(int argc, char *argv[])  
{ 
    char* inputFile;
    char* outputFile;
    int time, inputFd, outputFd, size, index = 0, openFlags, cont = 1, arr[2];
    char* wrBuf;
    struct flock lock;
    mode_t filePerms;
    ssize_t numRead, total = 0;
    char buf[NUM_OF_BYTES];
    char* str;

    int option;

    if(argc != 4){
        perror("Invalid number of arguments\n");
        return -1;
    }

    while((option = getopt(argc, argv, ":i:o:t:")) != -1){
        switch(option){
            case 't':
                time = atoi(optarg);
                break;
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

    while(cont){

        inputFd = open(inputFile, O_RDONLY);

        if (inputFd == -1)
            printf("opening file %s", argv[1]);

        openFlags = O_APPEND | O_CREAT | O_WRONLY;
        filePerms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

        outputFd = open(outputFile, openFlags, filePerms);
        if (outputFd == -1)
            printf("opening file %s", argv[2]);

        if(memset(&lock, 0, sizeof(lock)) == NULL)
            perror("Error in memset.\n");

        lock.l_type = F_WRLCK;        

        while ((numRead = read(inputFd, buf, NUM_OF_BYTES)) > 0){
            total += numRead;

            if(numRead < NUM_OF_BYTES){
                cont = 0;
                break;
            }

            wrBuf = malloc(sizeof(char) * (10*NUM_OF_BYTES));

            for (int i = 0; i < numRead; i++){
                arr[0] = buf[i];
                arr[1] = buf[i+1];
                ++i;
                str = malloc(sizeof(char)*7);
                strcat(wrBuf, convert(arr, str));
                str = NULL;
                free(str);
            }

            if(fcntl(outputFd, F_SETLK, &lock) < 0)
                printf("Error while locking the input file for writing. %s\n",  strerror(errno));

            strcat(wrBuf, "\n");
            
            if (write(outputFd, wrBuf, strlen(wrBuf)) < 0)
                perror("couldn't write whole buffer");

            // if (write(outputFd, "\n", sizeof(char)*strlen("\n")) < 0)
            //     perror("couldn't write whole buffer");

            total += sizeof(wrBuf);
            if(lseek(outputFd, strlen(wrBuf), SEEK_CUR) < 0)
                perror("lseek Error \n");

            lock.l_type = F_UNLCK;
            if(fcntl(outputFd, F_SETLK, &lock) < 0)
                perror("Error while unlocking the input file for writing.\n");
            wrBuf = NULL;
            free(wrBuf);
            printf("Sleeping for %d milliseconds(%d seconds)...\n", (int)time/1000, time);
            sleep(time);
        }

        if (numRead == -1)
            perror("read");
        if (close(inputFd) == -1)
            perror("close input");
        if (close(outputFd) == -1)
            perror("close output");        
    }    
    return 0; 
} 

char* convert(int arr[], char* str)
{
    sprintf(str, "%d+i%d,", arr[0], arr[1]);
    return str;
}