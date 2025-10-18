#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define OUTER_LOOP_CPU 1000
#define INNER_LOOP_CPU 10
#define LOOP_IO 10
#define PROCESS_PER_NICE 10
#define NICE_COUNT 6

void load_cpu() {
    volatile int i, j;
    volatile int sum = 0;

    for (i = 0; i < OUTER_LOOP_CPU; i++) {
        for (j = 0; j < INNER_LOOP_CPU; j++) {
            // Perform some basic arithmetic operations
            sum += (i * j) + (i % (j + 1)) - (j % (i + 1));
        }
    }
}

void load_io() {
    for (int i = 0; i < LOOP_IO; i++) {
        sleep(1);
    }
}

int main(int argc, char *argv[]) {
    int pid;

    printf("Benchmark pid: %d\n", getpid());


    // for (int i = 1; i <= NICE_COUNT; i++) {
        
    //     for (int j = 0; j < PROCESS_PER_NICE; j++) {
    //         pid = fork();
            
    //         if (pid < 0) {
    //             printf("fork failed\n");
    //             exit(1);
    //         }

    //         if (pid == 0) {
    //             printf("Benchmark pid: %d\n", getpid());
    //             setNice(i);
    //             int time = uptime(); // use uptime() for generating random number
    //             if (time % 2 == 0) load_cpu();
    //             else load_io();

    //             exit(0);
    //         }
    //     }
    // }

    for (int i = 1; i <= NICE_COUNT; i++) {
        for (int j = 0; j < PROCESS_PER_NICE; j++) {
            pid = fork();
            sleep(10);
            if (pid < 0) {
                printf("Fork failed\n");
                exit(1);
            } else if (pid == 0) {
                printf("Benchmark pid: %d\n", getpid());
                setnice(i);
                if (getpid() % 2 == 0) load_cpu();
                else load_io();
                printf("\nwe dont get here\n");
            }
        }
    }

    

    for (int i = 0; i < NICE_COUNT * PROCESS_PER_NICE; i++) {
        wait(0);
    }
    
    exit(0);
}