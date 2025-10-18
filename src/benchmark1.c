#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PROC_COUNT 4
#define ITERATIONS 1

void io_load() {
  for (int i = 0; i < 5; i++)
    printf(">>>>>>>>>>>>");
}

int main() {
  volatile int i;
  
  printf("Benchmark pid: %d\n", getpid());

  for (i = 1; i < PROC_COUNT; i++) {
    int pid = fork();
    setnice((i+2 % 4) * 5);
    if (pid < 0) {
      printf("Fork failed\n");
      exit(1);
    } else if (pid == 0) {
      printf("Benchmark pid: %d\n", getpid());
      break;
    }
  }

  int my_pid = getpid();

  if (my_pid % 2 == 0) { 
    volatile int x = 0;
    while(1) {
      for (int i = 0; i < 100; i ++)
        x ++;
      sleep(2);
    }
  } else { 
    while(1)
      io_load();
  }

  exit(0);
}