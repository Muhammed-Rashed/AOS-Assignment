#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <poll.h>

// Check if a string contains only digits, used to check if a directory name in /proc is a PID
int is_PID(const char *str) 
{
    for (int i = 0; str[i]; i++) 
    {
        if (!isdigit(str[i])) return 0;
    }
    return 1;
}

// Count total number of processes in /proc
int count_processes() 
{
    DIR *dir = opendir("/proc");
    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL) 
    {
        if (is_PID(entry->d_name)) count++;
    }

    closedir(dir);
    return count;
}

// Print all processes
void print_processes() 
{
    DIR *dir = opendir("/proc");
    if (!dir) 
    {
        perror("Failed to open /proc");
        return;
    }
    struct dirent *entry;

    printf("%-7s %-20s\n", "PID", "NAME");
    printf("---------------------------------------\n");

    while ((entry = readdir(dir)) != NULL) 
    {
        // filter only PIDs
        if (is_PID(entry->d_name)) 
        {
            char path[256];
            // build path in the PID directory
            sprintf(path, "/proc/%s/comm", entry->d_name);

            // open that PID directory to get the process name
            FILE *f = fopen(path, "r");
            if (f) 
            {
                char name[100];
                // get the process name
                fgets(name, sizeof(name), f);

                // Remove newline character
                name[strcspn(name, "\n")] = 0;

                printf("%-7s %-20s\n", entry->d_name, name);

                fclose(f);
            }
        }
    }

    closedir(dir);
}

// Draw the # bar simulating percentage, 30 slots = 100%
void print_bar(double percentage) 
{
    int total_slots = 30;
    int filled = (percentage / 100.0) * total_slots;

    printf("[");
    for (int i = 0; i < total_slots; i++) 
    {
        if (i < filled) printf("#");
        else printf("-");
    }
    printf("]");
}

// get the system uptime from /proc/uptime
int get_uptime()
{
    FILE *f = fopen("/proc/uptime", "r");
    if(!f)
    {
        perror("failed to open /proc/uptime");
        return 0;
    }
    float uptime;
    fscanf(f, "%f", &uptime);
    fclose(f);
    return (int)uptime;
}

int main() 
{
    // user is time CPU spends running normal user processes
    // nice is time CPU takes executing low-priority processes
    // system is time CPU spends executing OS code
    // idle is time CPU spends doing nothing
    long long user, nice, system, idle;
    long long prev_idle = 0, prev_total = 0;

    // This struct tells the OS which file descriptor to monitor and what events to look for
    struct pollfd fds[1];
    fds[0].fd = STDIN_FILENO;  // Sets the file descriptor to std input(keyboard)
    fds[0].events = POLLIN;    // We are interested in read events (notify when data is available to read)

    while (1) 
    {
        // Clear screen to refresh display
        system("clear");

        // Read CPU statistics from /proc/stat
        FILE *file = fopen("/proc/stat", "r");
        if(!file) 
        {
            perror("Failed to open /proc/stat");
            return 1;
        }
        fscanf(file, "cpu %lld %lld %lld %lld", &user, &nice, &sys, &idle);
        fclose(file);
        
        long long total = user + nice + sys + idle;
        double cpu_usage = 0.0;

        // Calculate CPU usage using previous values, using delta method
        if (prev_total != 0) 
        {
            long long total_diff = total - prev_total;
            long long idle_diff = idle - prev_idle;
            if (total_diff > 0) cpu_usage = (double)(total_diff - idle_diff) / total_diff * 100.0;
        }

        prev_total = total;
        prev_idle = idle;

        // Read memory information from /proc/meminfo
        FILE *memfile = fopen("/proc/meminfo", "r");
        if(!memfile) 
        {
            perror("Failed to open /proc/meminfo");
            return 1;
        }
        long long memTotal = 0, memFree = 0, value;
        char label[64];

        while (fscanf(memfile, "%s %lld", label, &value) != EOF) 
        {
            if (strcmp(label, "MemTotal:") == 0) memTotal = value;
            else if (strcmp(label, "MemFree:") == 0) memFree = value;
        }

        fclose(memfile);

        long long used = memTotal - memFree;
        double mem_usage = (double)used / memTotal * 100.0;

        // Count processes
        int proc_count = count_processes();

        // Display
        printf("Mini-HTOP\n");
        printf("Press n then Enter to quit\n\n");
        printf("CPU   "); 
        print_bar(cpu_usage); 
        printf("  %6.2f%%\n", cpu_usage);
        printf("MEM   "); 
        print_bar(mem_usage);
        printf("  %6.2f%% (%lld / %lld MB)\n", mem_usage, used/1024, memTotal/1024);
        printf("PROC %d\n", proc_count);
        printf("UPTIME %d sec\n", get_uptime());
        print_processes();

        // Exiting using N key
        
        // This waits for 1 sec or until Enter is pressed
        // This pauses the program for 1 sec
        int activity = poll(fds, 1, 1000);

        // Checks what happened in this 1 sec
        // If activity > 0 that mean something happend before this 1 second passed
        // else if activity == 0 that means the 1 sec simply passed with no input and the loop continues
        if (activity > 0) {
            // This confirms that what happened was actually a keypress
            if(fds[0].revents & POLLIN)
            {
                char input[10];
                fgets(input, sizeof(input), stdin); // Reads the typed line
                if (input[0] == 'n' || input[0] == 'N') break;  // If the first char is n, the program exits
            }
        }
    }

    return 0;
}