#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // For sleep()

#define MAX_PROCESSES 100   // Maximum number of processes
#define MAX_TIME 1000       // Maximum time slices
#define TIME_SLICE_STEP 180  // Chart update interval (20 time slices)
#define BUFFER_SIZE 8192    // Size of buffer to read the file

int processDetails[MAX_PROCESSES][3]; // start time, last slice, currently burst time
int processAvailable[MAX_PROCESSES]; 

void draw_chart(int active[MAX_PROCESSES][MAX_TIME], int max_process, int start_time, int end_time) {
    for (int p = 0; p <= max_process; p++) {
        if(processAvailable[p] == 0) continue; 
        printf("p%d ", p); // Print process label
        for (int t = start_time; t <= end_time; t++) {
            if (active[p][t]) {
                printf("+"); // Active time slice
            } else {
                printf("-"); // Inactive time slice
            }
        }
        printf("\n");
    }
    printf("local details of processes:\n");
    int avgWaitingTime = 0, processWaitingTime = 0;
    int num = 0;
    for (int p = 0; p <= max_process; p++) {
        if (processAvailable[p] == 0) 
            continue;
        num ++;
        processWaitingTime = processDetails[p][1] - processDetails[p][0] -  processDetails[p][2];
        processWaitingTime = processWaitingTime > 0 ? processWaitingTime : 0;
        printf("process %d -> turnaround time: <%d> |   waiting time: <%d>\n", p, processDetails[p][1] - processDetails[p][0], processWaitingTime);
        avgWaitingTime += processWaitingTime;
    }
    max_process = max_process > 0 ? max_process : 1;
    printf("average waiting time: %d\n", avgWaitingTime / num);
    printf("\n");
}

int main() {

    char log_file[] = "logs";
    FILE *file = fopen(log_file, "r");
    if (!file) {
        perror("Failed to open the log file");
        return 1;
    }
     for (int p = 0; p < MAX_PROCESSES; p++) {
            processDetails[p][0] = 999999999; 
            processDetails[p][2] = 0; 
            processAvailable[p] = 0;
     }

    
    int active[MAX_PROCESSES][MAX_TIME] = {0};// array to tracking status

    int max_process = 0;
    int last_read_position = 0; // Track the last read position in the file
    int current_time = 0;       // Track the current time slice

    while (1) {
        fseek(file, last_read_position, SEEK_SET); // Move to the last read position
        char buffer[BUFFER_SIZE];
        size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, file); // Read new data
        buffer[bytes_read] = '\0';

        char *token = strtok(buffer, ">>"); // Split the data by ">>"
        while (token != NULL) {
            int p, start, end, benchmark_pid;

            // Parse the process, start, and end times
            if (sscanf(token, "{p%d,s%d,e%d}", &p, &start, &end) == 3) {
                processAvailable[p] = 1;
                processDetails[p][0] =  processDetails[p][0] < start ?  processDetails[p][0] : start; 
                if (p > max_process) max_process = p;     // Track the highest process number
                if (end > current_time) current_time = end; // Update the highest time slice

                // Mark the time slices as active for this process
                if (start == end) end ++; 
                for (int t = start; t < end; t++) {
                    if (active[p][t] == 0) processDetails[p][2] += 1;
                    active[p][t] = 1;
                    processDetails[p][1] = processDetails[p][1] < start ? start :  processDetails[p][1];
                }
            }
            // if (sscanf(token, "{p%d,s%d,e%d}Benchmark pid: %d", &p, &start, &end, &benchmark_pid) == 4) {
            //     processAvailable[p] = 1;
            //     processDetails[p][0] = start; // Save the start time of the benchmarked process
            //     processDetails[p][2] = 0; // Initial Burst time
            //     processDetails[p][1] = start; // initial End time
            //     printf("Benchmark detected for pid: %d, start time: %d\n", benchmark_pid, start);
            // }

            token = strtok(NULL, ">>");
        }

        // Update the last read position
        last_read_position = ftell(file);

        // Calculate the time range for the current update
        int start_time = current_time > TIME_SLICE_STEP ? current_time - TIME_SLICE_STEP : 0;
        int end_time = current_time;

        // Clear the screen for a fresh chart
        system("clear");

        // Draw the updated chart
        printf("Process Activity Chart (Time: %d to %d):\n", start_time, end_time);
        draw_chart(active, max_process, start_time, end_time);

        // Wait for a short period before updating again
        sleep(10); // Simulate real-time updates (adjust if necessary)
    }

    fclose(file);
    return 0;
}