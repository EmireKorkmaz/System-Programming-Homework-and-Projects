#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>

#define MAX 1024

int timeStamp() {
    time_t date=time(NULL);
    char* time = (char*)malloc(sizeof(char)*strlen(asctime(localtime(&date))));
    strncpy(time, asctime(localtime(&date)),strlen(asctime(localtime(&date))));
    time[strlen(time)-1]='\0';
    printf("%s : ", time);
    free(time);
    return 0;
}

int main(int argc, char *argv[]) {

	int option, PORT, source, destination;
	char* address;

	if(argc != 9){
		fprintf(stderr,"Error in the number of the given arguments\n");
		exit(-1);
	}

	while((option = getopt(argc, argv, ":a:p:s:d:")) != -1){
		if(option == 'a') {
			address = optarg;
		}
		else if(option == 'p')
			PORT = atoi(optarg);
		else if(option == 's')
			source = atoi(optarg);
		else if(option == 'd')
			destination = atoi(optarg);
		else{
			fprintf(stderr, "unknown option\n");
			exit(1);
		}
	}

	int socket2;
	struct sockaddr_in server;
	char path[MAX];

	socket2 = socket(AF_INET, SOCK_STREAM, 0);
	if (socket2 == -1){
			perror("Error socket");
			exit(-1);
	}

	server.sin_addr.s_addr = inet_addr(address);
	server.sin_family = AF_INET;
	server.sin_port = htons(PORT);

	if (connect(socket2, (struct sockaddr *)&server , sizeof(server)) < 0){
		perror("Error connect");
		exit(-1);
	}

	char msg[MAX];
	timeStamp();
	printf("Client (%d) connecting to %s\n", getpid(), address);

	sprintf(msg,"%d %d %d", source, destination,  getpid());

	clock_t begin = clock();

	if(send(socket2, msg, strlen(msg), 0) < 0){
		perror("Error send");
		exit(-1);
	}

	timeStamp();
	printf("Client (%d) connected and requesting a path from node %d to %d\n", getpid(), source, destination);

	while(recv(socket2, path, MAX, 0) < 0){
		perror("recv failed");
		break;
	}

	clock_t end = clock();
	double timeSpent = (double)(end - begin) / CLOCKS_PER_SEC;
	timeStamp();
	printf("Server’s response to (%d): %s arrived in %.5lf seconds\n", getpid(), path, timeSpent);

	if(close(socket2)!=0){
		fprintf(stderr,"Error close\n");
		exit(-1);
	}

	return 0;
}
