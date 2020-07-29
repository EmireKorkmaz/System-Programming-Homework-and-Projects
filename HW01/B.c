#include <stdio.h>  
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <math.h>
#include <complex.h>
#include <ctype.h>

#define NUM_OF_BYTES 128

double PI;
typedef double complex cplx;

char* calculate(char* arr[], char* str);
 
void _fft(cplx buf[], cplx out[], int n, int step)
{
    if (step < n) {
        _fft(out, buf, n, step * 2);
        _fft(out + step, buf + step, n, step * 2);
 
        for (int i = 0; i < n; i += 2 * step) {
            cplx t = cexp(-I * PI * i / n) * out[i + step];
            buf[i / 2]     = out[i] + t;
            buf[(i + n)/2] = out[i] - t;
        }
    }
}
 
void fft(cplx buf[], int n)
{
    cplx out[n];
    for (int i = 0; i < n; i++) out[i] = buf[i];
 
    _fft(buf, out, n, 1);
}

int main(int argc, char *argv[])  
{ 
    char* inputFile;
    char* outputFile;
    int time, inputFd, outputFd, size = 0, index = 0, openFlags, fd, total = 0, line = 0, totalBytes = 0;
    char buffer[NUM_OF_BYTES];
    struct flock lock;
    mode_t filePerms;
    ssize_t numRead;
    char buf[NUM_OF_BYTES], c[1];
    struct stat st;

    int option, i=0;

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

    while(1){

        inputFd = open(inputFile, O_RDWR);

        if (inputFd == -1)
            printf("opening file %s", argv[1]);

        openFlags = O_APPEND | O_CREAT | O_WRONLY;
        filePerms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
        outputFd = open(outputFile, openFlags, filePerms);
        
        if (outputFd == -1)
            printf("opening file %s", argv[2]);

        if(memset(&lock, 0, sizeof(lock)) == NULL)
            perror("Error in memset.\n");       

         

        while ((numRead = read(inputFd, c, sizeof(char))) > 0){
			if(c[0] == '\n')
				size++;
		}	

        index = rand() % (size - 1);
                lseek(inputFd, 0, SEEK_SET);

		while ((numRead = read(inputFd, c, sizeof(char))) > 0){
			totalBytes += sizeof(char)*strlen(c);
			if(c[0] == '\n')
				line++;
			if(line == index -1)
				break;
		}
        
        lseek(inputFd, totalBytes, SEEK_SET);

        while ((numRead = read(inputFd, buf, NUM_OF_BYTES)) > 0){  
        	total += numRead;
        	if(sizeof(buf) > 0 && strcmp(buf, "\n") != 0 ){
                int sizeOfBuffer = strlen(buf) * sizeof(char);
                char* spaces = malloc(sizeof(char)*sizeOfBuffer);
                spaces[0] = '\n';

                for (int i = 1; i < sizeOfBuffer; ++i)
                    spaces[i]=' ';

                lock.l_type = F_WRLCK;
	            if(fcntl(outputFd, F_SETLK, &lock) < 0)
	                printf("Error while locking the input file for writing. %s\n",  strerror(errno));           
	            
                const char s[3] = "+i";
                char *token;

                token = strtok(buf, s);

                int j=0, l=0;
                char* arr[2];

                while( token != NULL && j<16) {
                    if(j==2){
                        char* str = malloc(sizeof(char)*255);
                        str = calculate(arr, str);

                        if (write(outputFd, str, sizeof(char)*strlen(str)) < 0)
                            perror("couldn't write the whole buffer");               

                        free(str);
                        j=0;
                    }
                    arr[j] = token;
                    j++;                    
                    token = strtok(NULL, s);
                }

	            if (write(outputFd, "\n", sizeof(char)*strlen("\n")) < 0)
	                perror("couldn't write the whole buffer");
                

	            lock.l_type = F_UNLCK;

	            if(fcntl(outputFd, F_SETLK, &lock) < 0)
	                perror("Error while unlocking the input file for writing.\n");

	            //locking the output file for the replacement

	            lock.l_type = F_WRLCK; 
	            if(fcntl(inputFd, F_SETLK, &lock) < 0)
	               printf("Error while locking the input file for writing. %s\n",  strerror(errno));

	            int bytes = lseek(inputFd, totalBytes, SEEK_SET);

	            if(bytes < 0){
	            	printf("%s\n", strerror(errno));
	            }

                if (write(inputFd, spaces, sizeOfBuffer) < 0)
                    perror("couldn't write the whole buffer");

                bytes = lseek(inputFd, totalBytes+(sizeOfBuffer * sizeof(char)), SEEK_SET);

	            lock.l_type = F_UNLCK;
	            if(fcntl(inputFd, F_SETLK, &lock) < 0)
	                perror("Error while unlocking the input file for writing.\n");

	            if(bytes < 0){
	            	printf("%s\n", strerror(errno));
	            }
                free(spaces);
	         }
            printf("Sleeping for %d milliseconds(%d seconds)...\n", (int)time/1000, time);
            sleep(time);
        }

        index = 0;
        line = 0;
        size = 0;
        totalBytes = 0;

		if (numRead == -1)
			perror("read");
		if (close(inputFd) == -1)
			perror("close input");
		if (close(outputFd) == -1)
			perror("close output"); 

    }

    return 0; 
} 

char* calculate(char* arr[], char* str)
{
    char *token;
    const char st[2] = ",";
    int l=0;
    int j=0;

    token = strtok(arr[1], st);

    cplx buf[1];
    for (int i = 0; i < 1; ++i){
        buf[i] =  atoi(arr[0])+atoi(arr[1])*I;
    }
    fft(buf, 1);
    char* string = malloc(sizeof(char)*7);
    for (int i = 0; i < 1; i++)
        if (!cimag(buf[i])){
            sprintf(string, "%g,", creal(buf[i]));
            strcat(str, string);
        }
        else{
            sprintf(string, "%g %g,", creal(buf[i]), cimag(buf[i]));
            strcat(str, string);
        }
    free(string);
    return str;
}
