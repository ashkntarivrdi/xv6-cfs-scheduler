#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PROC_COUNT 8
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
    if (pid < 0) {
      printf("Fork failed\n");
      exit(1);
    } else if (pid == 0) {
      printf("Benchmark pid: %d\n", getpid());
      break;
    }
  }

  int my_pid = getpid();

  if (my_pid % 2 == 0) { // p4
    volatile int x = 0;
    while(1) {
      x += 1;
    }
  } else { // p3
    while(1)
      io_load();
  }

  exit(0);
}
