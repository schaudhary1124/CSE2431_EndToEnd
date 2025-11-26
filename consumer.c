#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

/*
 * Consumer program responsibilities:
 *  - Start as a separate process exec'd by the producer
 *  - Create a listening TCP socket on localhost:PORT
 *  - accept() a connection from the producer
 *  - Create 2 threads that receive integers via the socket
 *  - Safely insert received integers into a shared global array
 *  - Print required status line for each insertion
 */

#define NUM_THREADS 2
#define PORT 12345
#define MAX_DATA 100

// Shared data array and insertion index
int data_array[MAX_DATA];
int data_index = 0;  // Next insertion index

int conn_fd = -1; // Socket file descriptor for connection

pthread_mutex_t array_mutex = PTHREAD_MUTEX_INITIALIZER;

// Timeout support so running ./consumer alone doesn't hang forever
static volatile sig_atomic_t timed_out = 0;

void alarm_handler(int sig) {
    (void)sig;
    timed_out = 1;
}

void *consumer_thread_func(void *arg) {
    (void)arg;
    // Stub function for consumer threads
    // Should read from socket and insert into data_array
    pid_t pid = getpid();
    pthread_t tid = pthread_self();

    while(1) {
        pthread_mutex_lock(&array_mutex);

        // Check if we've filled the array
        if (data_index >= MAX_DATA) {
            pthread_mutex_unlock(&array_mutex);
            break;  // Array full
        }

        // Read an integer from the socket
        uint32_t net_val;
        ssize_t n  = recv(conn_fd, &net_val, sizeof(net_val), MSG_WAITALL);
        if (n == 0) {
            // Connection closed by producer
            pthread_mutex_unlock(&array_mutex);
            break;
        } else if (n < 0) {
            perror("recv failed");
            pthread_mutex_unlock(&array_mutex);
            break;
        } else if (n != sizeof(net_val)) {
            // Partial read, handle as needed
            fprintf(stderr, "Partial read from socket\n");
            pthread_mutex_unlock(&array_mutex);
            break;
        }
        // Convert back from network byte order to host integer
        int value = (int)ntohl(net_val);
        // Insert into array and advance index
        data_array[data_index++] = value;

        printf("Consumer PID %d, Thread ID %lu inserted data element %d\n", pid, (unsigned long)tid, value);

        pthread_mutex_unlock(&array_mutex);
    }
    return NULL;
}

int main() {
    // Setup socket to accept connection from producer
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Allow reuse of address
    int opt_val = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val)) < 0) {
        perror("setsockopt failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // Bind and listen
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    #ifdef __APPLE__
    addr.sin_len = sizeof(addr);
    #endif
    
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    if (listen(listen_fd, 1) < 0) {
        perror("listen failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // Set up alarm for timeout
    signal(SIGALRM, alarm_handler);
    alarm(5); // Set timeout for 5 seconds

    // Accept connection from producer
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
    if (conn_fd < 0) {
        if (timed_out) {
            fprintf(stderr, "No producer connected within timeout period\n");
            close(listen_fd);
            pthread_mutex_destroy(&array_mutex);
            return 0; // Exit gracefully
        } else {
            perror("accept failed");
            close(listen_fd);
            pthread_mutex_destroy(&array_mutex);
            exit(EXIT_FAILURE);
        }
    }
    alarm(0); // Cancel alarm
    close(listen_fd); // No longer need the listening socket
    // (Socket creation and accept code stub)

    // Create consumer threads
    pthread_t threads[NUM_THREADS];
    int created = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, consumer_thread_func, NULL) != 0) {
            perror("pthread_create");
            break;
        }
        created++;
    }
    //appropriate code to handle thread exit
    for (int i = 0; i < created; i++) {
        pthread_join(threads[i], NULL);
    }
    // Close socket and cleanup
    close(conn_fd);
    pthread_mutex_destroy(&array_mutex);

    return 0;
}
