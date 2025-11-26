#include <stdio.h> 
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

/*
 * Producer program responsibilities:
 * - Start as parent process
 * - fork() and exec() the consumer process
 * - Connect to consumer via a TCP socket (localhost)
 * - Create 2 threads that read integers from a file and send them to consumer
 * - Print required status line for each item read
 */

#define NUM_THREADS 2
#define PORT 12345
#define MAX_DATA 100

/* Shared state for the producer threads */
static FILE *input_file = NULL;
static int socket_fd = -1;
static int numbers_read = 0;  // Count of numbers read so far
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

void *producer_thread_func(void *arg) {
    (void)arg;
    // Stub function for producer threads
    // Print example message as placeholder
    pid_t pid = getpid();
    pthread_t tid = pthread_self();

    while (1) {
        int value;

        // Critical section: file read + shared counter update
        pthread_mutex_lock(&file_mutex);

        // Stop once we've read MAX_DATA numbers total
        if (numbers_read >= MAX_DATA) {
            pthread_mutex_unlock(&file_mutex);
            break;
        }

        // Read next integer; if file ends early or error occurs, stop producing
        if (fscanf(input_file, "%d", &value) != 1) {
            pthread_mutex_unlock(&file_mutex);
            break;
        }
        numbers_read++;
        printf("Producer PID %d, Thread ID %lu read data element %d\n",
               pid, (unsigned long)tid, value);
        // End of critical section
        pthread_mutex_unlock(&file_mutex);
        
        uint32_t net_val = htonl((uint32_t)value);
        ssize_t sent_bytes = send(socket_fd, &net_val, sizeof(net_val), 0);

        // If send fails or sends partial bytes, stop this thread
        if (sent_bytes != (ssize_t)sizeof(net_val)) {
            perror("send failed");
            break;
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE to handle send errors gracefully
    const char *filename = "numbers.txt"; // Input file with integers
    // Input file is numbers.txt by default, but can be overridden via argv[1]
    if (argc >= 2) {
        filename = argv[1];
    }
    // Step 1: Fork and exec the consumer process (path to consumer binary needed)
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        // Child process exec consumer
        execl("./consumer", "consumer", NULL);
        // Only reached if execl fails
        perror("execl failed");
        exit(EXIT_FAILURE);
    }

    // Open the input file that contains integers to be sent
    input_file = fopen(filename, "r");
    if(!input_file) {
        perror("fopen numbers.txt");
        int status;
        kill(pid, SIGTERM); // Ensure child is killed if we fail here
        waitpid(pid, &status, 0);  // Wait for child to prevent zombie
        exit(EXIT_FAILURE);
    }

    // Consumer listens on localhost:PORT
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    // Use loopback directly (avoids inet_pton/inet_addr issues on macOS)
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    #ifdef __APPLE__
    // macOS specific: explicitly set length to avoid EINVAL
    server_addr.sin_len = sizeof(server_addr);
    #endif

    // Retry connecting until consumer is ready
    int retries = 0;
    const int MAX_RETRIES = 50;

    // Step 2: Set up a socket connection (Retry loop for macOS robustness)
    while (1) {
        // IMPORTANT: Create a fresh socket for every attempt
        // On macOS, a failed connect() renders the socket unusable
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0) {
            perror("socket creation failed");
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            fclose(input_file);
            exit(EXIT_FAILURE);
        }

        if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
            // Connection successful
            break;
        }

        // Connection failed - close this socket and retry
        close(socket_fd);
        socket_fd = -1; // Reset fd

        if (errno == ECONNREFUSED || errno == ENETUNREACH) {
            usleep(100000); // wait 100ms before retrying
            retries++;
            if (retries > MAX_RETRIES) {
                fprintf(stderr, "Failed to connect after %d retries\n", MAX_RETRIES);
                kill(pid, SIGTERM);
                waitpid(pid, NULL, 0);
                fclose(input_file);
                exit(EXIT_FAILURE);
            }
        } else {
            // Some other error occurred
            perror("connect");
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            fclose(input_file);
            exit(EXIT_FAILURE);
        }
    }

    // Step 3: Create producer threads
    pthread_t threads[NUM_THREADS];
    int created = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        if(pthread_create(&threads[i], NULL, producer_thread_func, NULL) != 0) {
            perror("pthread_create");
            break;
        }
        created++;
    }
    // Wait for the threads that started
    for(int i = 0; i < created; i++) {
        pthread_join(threads[i], NULL);
    }
    // Cleanup
    close(socket_fd);
    fclose(input_file);
    pthread_mutex_destroy(&file_mutex);
    int status;
    waitpid(pid, &status, 0);
    //appropriate code to handle thread exit
    // Close socket and cleanup
    return 0;
}