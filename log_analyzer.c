#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <limits.h>

#define MAX_LOG_LEN 2048
#define MAX_PATTERNS 10
#define MAX_EXTENSIONS 5
#define INOTIFY_BUFFER_SIZE (1024 * (sizeof(struct inotify_event) + 16))

typedef struct {
    char *pattern;
    int occurrences;
} LogPattern;

// Function prototypes (add these above all functions)
int is_regular_file(const char *path);
int has_valid_extension(const char *filename);

// Global variables for patterns and aggregation
LogPattern patterns[MAX_PATTERNS];
int pattern_count = 0;
int total_logs = 0;
const char *valid_extensions[MAX_EXTENSIONS] = {".log", ".txt", ".conf", ".csv", ".md"};

// File pointer to write error lines to
FILE *error_log_file;

// Function to initialize patterns
void initialize_patterns() {
    patterns[0].pattern = "ERROR";
    patterns[1].pattern = "WARN";
    patterns[2].pattern = "CRITICAL";
    pattern_count = 3;

    for (int i = 0; i < pattern_count; i++) {
        patterns[i].occurrences = 0;
    }
}

// Function to check for error patterns in a line
int check_for_error_patterns(const char *line) {
    return (strstr(line, "ERROR") != NULL ||
            strstr(line, "WARN") != NULL ||
            strstr(line, "CRITICAL") != NULL);
}

// Function to read and analyze logs from a file
void read_logs(char *log_file_path) {
    FILE *log_file = fopen(log_file_path, "r");
    if (log_file == NULL) {
        perror("Failed to open log file");
        return;
    }

    char log_line[MAX_LOG_LEN];
    while (fgets(log_line, sizeof(log_line), log_file)) {
        total_logs++;

        // Check for patterns and print/save the error lines
        for (int i = 0; i < pattern_count; i++) {
            if (strstr(log_line, patterns[i].pattern) != NULL) {
                patterns[i].occurrences++;

                // Print the line containing the error
                printf("Error found in %s: %s", log_file_path, log_line);

                // Write the error line to the error log file
                fprintf(error_log_file, "Error found in %s: %s", log_file_path, log_line);
            }
        }
    }

    fclose(log_file);
}

// Function to monitor and handle new/modified lines in a log file
void monitor_log_file(int inotify_fd, const char *log_file_path) {
    FILE *log_file;
    char buffer[MAX_LOG_LEN];

    log_file = fopen(log_file_path, "r");
    if (log_file == NULL) {
        perror("Failed to open log file");
        return;
    }

    // Read through the file and check for patterns
    while (fgets(buffer, sizeof(buffer), log_file)) {
        buffer[strcspn(buffer, "\r\n")] = 0; // Remove newline characters

        // Check for error patterns and print them
        if (check_for_error_patterns(buffer)) {
            printf("New error detected in %s: %s\n", log_file_path, buffer);
        }
    }

    fclose(log_file);
}

// Function to monitor multiple log files in a directory in real-time
void monitor_directory(const char *dir_path) {
    DIR *d;
    struct dirent *dir;
    int inotify_fd;
    char full_path[PATH_MAX];

    // Initialize inotify
    inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        perror("Failed to initialize inotify");
        return;
    }

    // Open the directory
    d = opendir(dir_path);
    if (!d) {
        perror("Unable to open directory");
        return;
    }

    // Add inotify watches for each file in the directory
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, dir->d_name);

        // Check if the file has a valid extension and is a regular file
        if (is_regular_file(full_path) && has_valid_extension(dir->d_name)) {
            int wd = inotify_add_watch(inotify_fd, full_path, IN_MODIFY);
            if (wd == -1) {
                perror("Failed to add inotify watch");
            } else {
                printf("Monitoring file: %s\n", full_path);
            }
        }
    }

    closedir(d);

    // Monitoring loop for inotify events
    while (1) {
        char event_buf[INOTIFY_BUFFER_SIZE];
        int event_len = read(inotify_fd, event_buf, INOTIFY_BUFFER_SIZE);

        if (event_len < 0) {
            perror("Failed to read inotify events");
            break;
        }

        // Process each inotify event
        int i = 0;
        while (i < event_len) {
            struct inotify_event *event = (struct inotify_event *)&event_buf[i];

            if (event->mask & IN_MODIFY) {
                snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, event->name);
                monitor_log_file(inotify_fd, full_path); // Handle modified log file
            }

            i += sizeof(struct inotify_event) + event->len;
        }

        usleep(500000); // Sleep briefly before checking again
    }

    close(inotify_fd);
}

// Function to display statistics
void display_statistics() {
    printf("\n===== Log Statistics =====\n");
    printf("Total logs processed: %d\n", total_logs);
    for (int i = 0; i < pattern_count; i++) {
        printf("%s: %d occurrences\n", patterns[i].pattern, patterns[i].occurrences);
    }
    printf("==========================\n");
}

// Function to handle signals (e.g., termination)
void signal_handler(int sig) {
    if (sig == SIGINT) {
        printf("\nTerminating log analysis...\n");
        display_statistics();

        // Close the error log file before exiting
        if (error_log_file != NULL) {
            fclose(error_log_file);
        }

        exit(0);
    }
}

// Function to check if a file is a regular file (not a directory)
int is_regular_file(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

// Function to check if a file has a valid text-based extension
int has_valid_extension(const char *filename) {
    for (int i = 0; i < MAX_EXTENSIONS; i++) {
        if (strstr(filename, valid_extensions[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

// Function to process all logs in a directory
void scan_directory_and_process_logs(const char *dir_path) {
    DIR *d;
    struct dirent *dir;
    char full_path[PATH_MAX];

    d = opendir(dir_path);
    if (!d) {
        perror("Unable to open directory");
        return;
    }

    while ((dir = readdir(d)) != NULL) {
        // Skip `.` and `..`
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
            continue;

        // Create the full path to the file
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, dir->d_name);

        // Check if the file is a regular file and has a valid text-based extension
        if (is_regular_file(full_path) && has_valid_extension(dir->d_name)) {
            printf("Processing log file: %s\n", full_path);
            read_logs(full_path);
        }
    }

    closedir(d);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s [--monitor] <log_file_or_directory>\n", argv[0]);
        return 1;
    }

    int monitor_mode = 0;
    char *log_path = NULL;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--monitor") == 0) {
            monitor_mode = 1;
        } else {
            log_path = argv[i];
        }
    }

    if (log_path == NULL) {
        printf("No log file or directory specified.\n");
        return 1;
    }

    // Initialize signal handler for graceful termination
    signal(SIGINT, signal_handler);

    // Initialize patterns
    initialize_patterns();

    // Open the error log file for writing
    error_log_file = fopen("error_log.txt", "w");
    if (error_log_file == NULL) {
        perror("Failed to open error log file");
        return 1;
    }

    // Check if the provided log path is a file or directory
    struct stat path_stat;
    stat(log_path, &path_stat);

    if (S_ISDIR(path_stat.st_mode)) {
        // Argument is a directory
        if (monitor_mode) {
            printf("Real-time monitoring enabled for directory %s\n", log_path);
            monitor_directory(log_path); // Monitor the directory
        } else {
            scan_directory_and_process_logs(log_path); // Analyze all files in the directory
        }
    } else {
        // Argument is a file
        if (monitor_mode) {
            printf("Real-time monitoring enabled for file %s\n", log_path);
            monitor_log_file(0, log_path); // Monitor a single file
        } else {
            read_logs(log_path); // Analyze a single file
        }
    }

    // Display final statistics
    display_statistics();

    // Close the error log file
    if (error_log_file != NULL) {
        fclose(error_log_file);
    }

    return 0;
}
