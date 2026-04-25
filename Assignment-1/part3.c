#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

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

    printf("\nPID\tNAME\n");
    printf("-----------------------------\n");

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

                printf("%s\t%s\n", entry->d_name, name);

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

int main() 
{
    // user is time CPU spends running normal user processes
    // nice is time CPU takes executing low-priority processes
    // system is time CPU spends executing OS code
    // idle is time CPU spends doing nothing
    long long user, nice, system, idle;
    long long prev_idle = 0, prev_total = 0;

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
        fscanf(file, "cpu %lld %lld %lld %lld", &user, &nice, &system, &idle);
        fclose(file);
        
        long long total = user + nice + system + idle;
        double cpu_usage = 0.0;

        // Calculate CPU usage using previous values, using delta method
        if (prev_total != 0) 
        {
            long long total_diff = total - prev_total;
            long long idle_diff = idle - prev_idle;
            cpu_usage = (double)(total_diff - idle_diff) / total_diff * 100.0;
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
    }

    return 0;
}