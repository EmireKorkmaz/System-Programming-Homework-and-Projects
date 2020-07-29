#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <errno.h>

int *cooks, *students;
int *cooksSuspended;
int supplierDone=0;
char *file;
int N=0, M=0;

void catcher(int signum) {

    switch (signum) {
        case SIGUSR1: {
            *cooksSuspended = 0;
            break;
        }
        case SIGCHLD: {
            // change turn
            break;
        }
        case SIGUSR2: {
            // file is finished
            break;
        }
        case SIGINT: {
            puts("SIGINT has been caught.\n");
            for (int i = 0; i < N; ++i) {
                kill(SIGTERM, cooks[i]);
            }
            for (int i = 0; i < M; ++i) {
                kill(SIGTERM, students[i]);
            }
            free(file);
            exit(EXIT_SUCCESS);
        }
        default: {
            puts("default signal\n");
            break;
        }
    }
}

int fullStudents=0;

int main(int argc, char *argv[]) {

    int option, T=0, L=0, S=0, K=0, fileSize=0, fd;
    pid_t supplier;
    int *kitchen, *counter; // counter and kitchen are shared memories.
    char *filename="\0";
    int *counterDone, *supplierSuspended, *signalled, studentProcess, processes;
    int  *studentsSuspended, *studentSignalled;


    // -N 3 -M 12 -T 5 -S 4 -L 13
    while ((option = getopt(argc, argv, ":N:M:T:S:L:F:")) != -1) {
        switch (option) {
            case 'N':
                N = atoi(optarg);
                break;
            case 'M':
                M = atoi(optarg);
                break;
            case 'T':
                T = atoi(optarg);
                break;
            case 'S':
                S = atoi(optarg);
                break;
            case 'L':
                L = atoi(optarg);
                break;
            case 'F':
                filename = optarg;
                break;
            case '?':
                fprintf(stdout,"unknown option: %c\n", optopt);
                break;
            default:
                fprintf(stdout,"Unknown option\n");
                break;
        }
    }
    // Constraints
    /*
        M > N > 2
        S > 3
        M > T >= 1
        L >= 3
        K = 2LM+1

      */

    // N cooks, M students, T tables, a counter of size S, and a kitchen of size K.

    K = 2 * L * M + 1;

    if (!(((M > N) && (N > 2)) && (S > 3) && ((M > T) && (T >= 1)) && (L >= 3))) {
        fprintf(stderr, "Error in the given values\n");
        exit(-1);
    }

    fileSize = 3 * L * M;

    struct stat st1;
    if (stat(filename, &st1) < 0) {
        fprintf(stderr, "Error stat\n");
        exit(-1);
    }

    int size = st1.st_size;

    if (size == 0) {
        fprintf(stderr, "The given file should not be empty\n");
        exit(-1);
    }

    if (size < fileSize) {
        fprintf(stderr, "The given file size should be 3*L*M or higher.\n");
        exit(-1);
    }

    kitchen = (int *) mmap(NULL, 3 * sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    counter = (int *) mmap(NULL, 3 * sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);

    fd = open(filename, O_RDONLY);

    if (fd < 0) {
        perror("Error while opening the input file\n");
        return -1;
    }

    for (int k = 0; k < 3; ++k) {
        kitchen[k] = 0;
        counter[k] = 0;
    }

    struct sigaction sact;

    sigemptyset(&sact.sa_mask);
    sact.sa_flags = 0;
    sact.sa_handler = catcher;

    if (sigaction(SIGUSR1, &sact, NULL) != 0)
        perror("SIGUSR1 sigaction() error");
    if (sigaction(SIGUSR2, &sact, NULL) != 0)
        perror("SIGUSR2 sigaction() error");
    if (sigaction(SIGCHLD, &sact, NULL) != 0)
        perror("SIGCHLD sigaction() error");
    if (sigaction(SIGINT, &sact, NULL) != 0)
        perror("SIGINT sigaction() error");

    sem_t *kitchenMutex = (sem_t *) mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    sem_t *kitchenFull = (sem_t *) mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    sem_t *kitchenEmpty = (sem_t *) mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);

    sem_t *counterEmpty = (sem_t *) mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    sem_t *counterFull = (sem_t *) mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    sem_t *counterMutex = (sem_t *) mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);

    sem_t *tableFull = (sem_t *) mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    sem_t *tableEmpty = (sem_t *) mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    sem_t *tableMutex = (sem_t *) mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    sem_t *table = (sem_t *) mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);

    int *counterSize = (int*) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    int *counterCount = (int*) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    int *studentLoop = (int*) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);

//    supplierDone = (int*) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    counterDone = (int*) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    supplierSuspended = (int*) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    cooksSuspended = (int*) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    signalled = (int*) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    studentsSuspended = (int*) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    studentSignalled = (int*) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    students = (int*) mmap(NULL, sizeof(int)*M, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    cooks = (int*) mmap(NULL, sizeof(int)*N, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);

//    *supplierDone = 0;
    *counterDone = 0;
    *supplierSuspended = 0;
    *cooksSuspended = 1;
    *signalled = 0;
    studentProcess = 0;
    processes =0;
    *studentsSuspended = 1;
    *studentSignalled = 0;

    if (sem_init(kitchenEmpty, 1, K) < 0) {
        fprintf(stderr, "Error sem_init\n");
        exit(-1);
    }

    if (sem_init(kitchenFull, 1, 0) < 0) {
        fprintf(stderr, "Error sem_init\n");
        exit(-1);
    }

    if (sem_init(kitchenMutex, 1, 1) < 0) {
        fprintf(stderr, "Error sem_init\n");
        exit(-1);
    }

    if (sem_init(counterFull, 1, 0) < 0) {
        fprintf(stderr, "Error sem_init\n");
        exit(-1);
    }
    if (sem_init(counterEmpty, 1, S) < 0) {
        fprintf(stderr, "Error sem_init\n");
        exit(-1);
    }
    if (sem_init(counterMutex, 1, 1) < 0) {
        fprintf(stderr, "Error sem_init\n");
        exit(-1);
    }

    if (sem_init(tableEmpty, 1, T) < 0) {
        fprintf(stderr, "Error sem_init\n");
        exit(-1);
    }
    if (sem_init(tableFull, 1, 0) < 0) {
        fprintf(stderr, "Error sem_init\n");
        exit(-1);
    }
    if (sem_init(tableMutex, 1, 1) < 0) {
        fprintf(stderr, "Error sem_init\n");
        exit(-1);
    }
    if (sem_init(table, 1, T) < 0) {
        fprintf(stderr, "Error sem_init\n");
        exit(-1);
    }

    *counterSize = S;
    *studentLoop = L;
    supplier = getpid();

    for (int j = 0; j < N+M; ++j) {
        if (fork() == 0) {
            if(j<N) {
                cooks[j] = getpid();
                break;
            }
            else{
                students[j-N] = getpid();
                break;
            }

        }
    }

    if (getpid() == supplier) {

        file = (char *) malloc(sizeof(char) * fileSize);

        int numRead = 0, i = 0;
        char c;
        while ((numRead = read(fd, &c, sizeof(char))) > 0) {
            if (i == fileSize)
                break;
            file[i] = c;
            i++;
        }

        int count = 0;
        while (count < i) {
            int sum = kitchen[0] + kitchen[1] + kitchen[2];

            if (sum < K) {
                if (file[count] == 'P' || file[count] == 'p') {
                    if (sem_wait(kitchenEmpty) < 0) {
                        fprintf(stderr, "Error sem_wait P kitchenEmpty %s\n", strerror(errno));
                        exit(-1);
                    }

                    if (sem_wait(kitchenMutex) < 0) {
                        fprintf(stderr, "Error sem_wait P kitchenMutex %s\n", strerror(errno));
                        exit(-1);
                    }

                    kitchen[0]++;
                    count++;
                    fprintf(stdout, "The supplier is going to the kitchen to deliver soup: kitchen items P:%d,C:%d,D:%d=%d\n"
                                    ,kitchen[0], kitchen[1], kitchen[2], kitchen[0]+kitchen[1]+kitchen[2]);

                    if (sem_post(kitchenMutex) < 0) {
                        fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                        exit(-1);
                    }

                    if (sem_post(kitchenFull) < 0) {
                        fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                        exit(-1);
                    }

                    fprintf(stdout, "The supplier delivered soup – after delivery: kitchen items P:%d,C:%d,D:%d=%d\n"
                            ,kitchen[0], kitchen[1], kitchen[2], kitchen[0]+kitchen[1]+kitchen[2]);

                } else if (file[count] == 'C' || file[count] == 'c') {

                    if (sem_wait(kitchenEmpty) < 0) {
                        fprintf(stderr, "Error sem_wait C kitchenEmpty %s\n", strerror(errno));
                        exit(-1);
                    }
                    if (sem_wait(kitchenMutex) < 0) {
                        fprintf(stderr, "Error sem_wait C kitchenMutex %s\n", strerror(errno));
                        exit(-1);
                    }

                    count++;
                    kitchen[1]++;

                    fprintf(stdout, "The supplier is going to the kitchen to deliver main course: kitchen items P:%d,C:%d,D:%d=%d\n"
                            ,kitchen[0], kitchen[1], kitchen[2], kitchen[0]+kitchen[1]+kitchen[2]);

                    if (sem_post(kitchenMutex) < 0) {
                        fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                        exit(-1);
                    }

                    if (sem_post(kitchenFull) < 0) {
                        fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                        exit(-1);
                    }

                    fprintf(stdout, "The supplier delivered main course – after delivery: kitchen items P:%d,C:%d,D:%d=%d\n"
                            ,kitchen[0], kitchen[1], kitchen[2], kitchen[0]+kitchen[1]+kitchen[2]);

                } else if (file[count] == 'D' || file[count] == 'd') {

                    if (sem_wait(kitchenEmpty) < 0) {
                        fprintf(stderr, "Error sem_wait D kitchenEmpty %s\n", strerror(errno));
                        exit(-1);
                    }
                    if (sem_wait(kitchenMutex) < 0) {
                        fprintf(stderr, "Error sem_wait kitchenMutex %s\n", strerror(errno));
                        exit(-1);
                    }

                    count++;

                    kitchen[2]++;

                    fprintf(stdout, "The supplier is going to the kitchen to deliver dessert: kitchen items P:%d,C:%d,D:%d=%d\n"
                            ,kitchen[0], kitchen[1], kitchen[2], kitchen[0]+kitchen[1]+kitchen[2]);

                    if (sem_post(kitchenMutex) < 0) {
                        fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                        exit(-1);
                    }

                    if (sem_post(kitchenFull) < 0) {
                        fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                        exit(-1);
                    }

                    fprintf(stdout, "The supplier delivered dessert – after delivery: kitchen items P:%d,C:%d,D:%d=%d\n"
                            ,kitchen[0], kitchen[1], kitchen[2], kitchen[0]+kitchen[1]+kitchen[2]);
                }
                else{
                    fprintf(stderr, "File should not contain any letter but P, C, D. Exiting.\n");
                    free(file);
                    exit(-1);
                }
            }
            else if(sum >= K){
                fprintf(stdout,"The Supplier is suspended and waits for the cooks to free the kitchen at %d\n", count);

                sigset_t sigmask;
                if (sigemptyset(&sigmask) < 0) {
                    fprintf(stderr, "Error sigemptyset\n");
                    exit(-1);
                }

                *supplierSuspended = 1;
                while (sigsuspend(&sigmask) != -1)
                    ;

                *supplierSuspended = 0;
                fprintf(stdout,"The Supplier is back on\n");
            }
        }

        supplierDone = 1;
        free(file);

        fprintf(stdout,"The supplier finished supplying – GOODBYE!\n");

        pid_t wpid;
        int status = 0;
        for (int r = 0; r < N+M ; ++r) {
            wait(&status);
        }

        fprintf(stdout,"Waiting has been completed.\n");

        sem_destroy(kitchenEmpty);
        sem_destroy(kitchenFull);
        sem_destroy(kitchenMutex);
        sem_destroy(counterEmpty);
        sem_destroy(counterFull);
        sem_destroy(counterMutex);
        sem_destroy(tableFull);
        sem_destroy(tableEmpty);
        sem_destroy(tableMutex);

        exit(0);
    }

    while (processes < N) {
        if (getpid() == cooks[processes]) {

            fprintf(stdout, "Cook %d is going to the kitchen to wait for/get a plate - kitchen items\n"
                            "P:%d,C:%d,D:%d=%d\n", processes, kitchen[0], kitchen[1], kitchen[2],
                    kitchen[0] + kitchen[1] + kitchen[2]);

            while (supplierDone == 0 || kitchen[0] + kitchen[1] + kitchen[2] != 0) {
                int index=0;

                if (counter[0]+counter[1]+counter[2] < *counterSize) {

                    if (sem_wait(kitchenFull) < 0) {
                        fprintf(stderr, "Error sem_wait cook kitchenFull %s\n", strerror(errno));
                        exit(-1);
                    }
                    if (sem_wait(kitchenMutex) < 0) {
                        fprintf(stderr, "Error sem_wait cook kitchenMutex %s\n", strerror(errno));
                        exit(-1);
                    }

                    if (sem_wait(counterEmpty) < 0) {
                        fprintf(stderr, "Error sem_wait counter counterEmpty %s\n", strerror(errno));
                        exit(-1);
                    }
                    if (sem_wait(counterMutex) < 0) {
                        fprintf(stderr, "Error sem_wait counter counterMutex %s\n", strerror(errno));
                        exit(-1);
                    }

                    srand(getpid());
                    index= rand()%3;

                    if ((counter[0] < counter[1]) && (counter[0] < counter[2]) && kitchen[0] > 0) {
                        index = 0;
                    } else if (counter[1] < counter[2] && kitchen[1] > 0) {
                        index = 1;
                    } else if (kitchen[2] > 0) {
                        index = 2;
                    }

                    if(kitchen[index]>0) {
                        kitchen[index]--;
                    }
                    else{
                        if(kitchen[0]>0) {
                            kitchen[0]--;
                        }
                        else if(kitchen[1]>0) {

                            kitchen[1]--;
                        }
                        else if(kitchen[2]>0) {

                            kitchen[2]--;
                        }
                    }

                    counter[index]++;

                    if(index == 0) {
                        fprintf(stdout,
                                "Cook %d is going to the counter to deliver soup – counter items P:%d,C:%d,D:%d=%d\n",
                                processes, counter[0], counter[1], counter[2], counter[0] + counter[1] + counter[2]);
                    }
                    else if(index == 1) {
                        fprintf(stdout,"Cook %d is going to the counter to deliver main course – counter items P:%d,C:%d,D:%d=%d\n",
                                processes, counter[0], counter[1], counter[2], counter[0] + counter[1] + counter[2]);
                    }
                    else if(index == 2) {
                        fprintf(stdout,"Cook %d is going to the counter to deliver dessert – counter items P:%d,C:%d,D:%d=%d\n",
                                processes, counter[0], counter[1], counter[2], counter[0] + counter[1] + counter[2]);
                    }

                    if (*supplierSuspended == 1 && supplierDone==0) {
                        kill(supplier, SIGUSR1);
                    }

                    if (sem_post(counterMutex) < 0) {
                        fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                        exit(-1);
                    }

                    if (sem_post(counterFull) < 0) {
                        fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                        exit(-1);
                    }

                    if(index == 0) {
                        fprintf(stdout,
                                "Cook %d placed soup on the counter – counter items P:%d,C:%d,D:%d=%d\n",
                                processes, counter[0], counter[1], counter[2], counter[0] + counter[1] + counter[2]);
                    }
                    else if(index == 1) {
                        fprintf(stdout,"Cook %d placed main course on the counter – counter items P:%d,C:%d,D:%d=%d\n",
                                processes, counter[0], counter[1], counter[2], counter[0] + counter[1] + counter[2]);
                    }
                    else if(index == 2) {
                        fprintf(stdout,"Cook %d placed dessert on the counter  – counter items P:%d,C:%d,D:%d=%d\n",
                                processes, counter[0], counter[1], counter[2], counter[0] + counter[1] + counter[2]);
                    }

                    if (sem_post(kitchenMutex) < 0) {
                        fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                        exit(-1);
                    }
                    if (sem_post(kitchenEmpty) < 0) {
                        fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                        exit(-1);
                    }

                }

            }
            if(fullStudents == M-1) {
                fprintf(stdout, "Cook %d finished serving - items at kitchen: %d – going home – GOODBYE!!!\n"
                        , processes, kitchen[0] + kitchen[1] + kitchen[2]);
                _exit(0);
            }
        }
        processes = processes+1;
    }

    while (studentProcess < M) {
        if(getpid()==students[studentProcess]) {
            int loop = 1;
            while (loop < *studentLoop) {
                if (counter[0] > 0 && counter[1] > 0 && counter[2] > 0) {
                    fprintf(stdout, "Student %d is going to the counter (round %d) - # of students at counter: 1 and "
                                    "counter items P:%d,C:%d,D:%d=%d\n", studentProcess, loop, counter[0], counter[1], counter[2],counter[0]+ counter[1]+ counter[2]);
                    if (sem_wait(counterFull) < 0) {
                        fprintf(stderr, "Error sem_wait student counterFull %s\n", strerror(errno));
                        exit(-1);
                    }

                    if (sem_wait(counterMutex) < 0) {
                        fprintf(stderr, "Error sem_wait student counterMutex %s\n", strerror(errno));
                        exit(-1);
                    }

                    if (counter[0] > 0 && counter[1] > 0 && counter[2] > 0) {
                        counter[0]--;
                        counter[1]--;
                        counter[2]--;
                    }
                    else {
                        if (sem_post(counterMutex) < 0) {
                            fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                            exit(-1);
                        }
                        if (sem_post(counterFull) < 0) {
                            fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                            exit(-1);
                        }
                        continue;
                    }

                    loop++;

                    if (sem_post(counterMutex) < 0) {
                        fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                        exit(-1);
                    }

                    if (sem_post(counterEmpty) < 0) {
                        fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                        exit(-1);
                    }

                    int t;
                    sem_getvalue(table, &t);

                    fprintf(stdout,"Student %d got food and is going to get a table (round %d) - # of empty tables: %d\n", studentProcess, loop, t);


                    if (sem_wait(table) < 0) {
                        fprintf(stderr, "Error sem_wait table %s\n", strerror(errno));
                        exit(-1);
                    }
                    sem_getvalue(table, &t);
                    fprintf(stdout,"Student %d sat at table 1 to eat (round %d) - empty tables:%d\n", studentProcess,loop,t);

                    if (loop == *studentLoop) {
                        fprintf(stdout,"Student %d is done eating L=%d times - going home – GOODBYE!!!\n", studentProcess, loop);
                        fullStudents++;
                        _exit(0);
                    }
                    if (sem_post(table) < 0) {
                        fprintf(stderr, "Error sem_post %s\n", strerror(errno));
                        exit(-1);
                    }
                    sem_getvalue(table, &t);
                    fprintf(stdout,"Student %d left table %d to eat again (round %d) - empty tables:%d\n", studentProcess, t, loop, t);
                }
            }
        }
        studentProcess++;
    }
    return 0;
}