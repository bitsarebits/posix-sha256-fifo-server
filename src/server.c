#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <openssl/sha.h>
#include <pthread.h>

#include "errExit.h"
#include "request_response.h"

// Function prototypes

/**
 * Insert a new request into the request list.
 * If a request for the same file (same path and mtime) already exists,
 * only add the client to the waiting clients list.
 */
void update_request_list(struct Request *request);

/**
 * Function executed by worker threads.
 * Takes requests from the list, computes SHA256, and sends the response to clients.
 */
void *worker_thread(void *arg);

/**
 * Handles server termination: closes and removes the FIFO, terminates the process.
 */
void quit(int sig);

/**
 * Computes the SHA256 hash of a file and writes it to the hash array.
 */
void digest_file(const char *filename, uint8_t *hash);

/**
 * Sends a Response to the client through its FIFO.
 */
void sendResponse(struct Response *response, pid_t cPid);

// FIFO path for handling SHA256 requests from clients
char *path2ServerFIFO = "/tmp/fifo_server_SHA256";
char *baseClientFIFO = "/tmp/fifo_client_SHA256."; // completed with the process ID

// FIFO file descriptors
int serverFIFO, serverFIFO_extra;

// Node for the list of clients waiting for the same file hash
typedef struct client_node
{
    pid_t pid;
    struct client_node *next;
} client_node_t;

// Node for the request list
typedef struct request_list
{
    char pathname[PATH_MAX];
    time_t last_mod_time;
    size_t filesize;
    client_node_t *clients;
    struct request_list *next;
} request_list_t;

// Initialize the request list head, the mutex, and the condition variable for thread synchronization
request_list_t *request_list_head = NULL;
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t list_cond = PTHREAD_COND_INITIALIZER;

// Add a new request to the request list
void update_request_list(struct Request *request)
{
    // Read file stats to get the last modification time
    struct stat st;
    if (stat(request->pathname, &st) != 0)
        errExit("stat failed\n");

    // Lock the mutex to synchronize with other threads
    pthread_mutex_lock(&list_mutex);

    request_list_t *prev = NULL, *curr = request_list_head;

    while (curr)
    {
        if (strcmp(curr->pathname, request->pathname) == 0 &&
            curr->last_mod_time == st.st_mtime)
        {
            // Path and mtime already in the list, add the client PID
            // Only one thread will calculate the SHA256 and send to multiple clients
            client_node_t *new_client = malloc(sizeof(client_node_t));
            new_client->pid = request->cPid;
            new_client->next = curr->clients;
            curr->clients = new_client;

            // Release the mutex and return
            pthread_mutex_unlock(&list_mutex);
            return;
        }
        if (st.st_size < curr->filesize)
            break;
        prev = curr;
        curr = curr->next;
    }

    // New request: allocate and fill the request node
    request_list_t *new_req = malloc(sizeof(request_list_t));
    strncpy(new_req->pathname, request->pathname, PATH_MAX);
    new_req->last_mod_time = st.st_mtime;
    new_req->filesize = st.st_size;
    new_req->clients = malloc(sizeof(client_node_t));
    new_req->clients->pid = request->cPid;
    new_req->clients->next = NULL;

    // Insert the request into the list
    new_req->next = curr;
    if (prev)
        prev->next = new_req;
    else
        request_list_head = new_req;

    // Wake up a worker thread and release the mutex
    pthread_cond_signal(&list_cond);
    pthread_mutex_unlock(&list_mutex);
}

// Worker thread: handles client requests; waits on a condition variable if the list is empty
void *worker_thread(void *arg)
{
    while (1)
    {
        // Acquire the lock to access the request list
        pthread_mutex_lock(&list_mutex);

        // If the list is empty, wait on the condition variable
        while (!request_list_head)
        {
            pthread_cond_wait(&list_cond, &list_mutex);
        }

        // Remove a request from the head of the list
        request_list_t *req = request_list_head;
        request_list_head = request_list_head->next;

        // Unlock the mutex
        pthread_mutex_unlock(&list_mutex);

        // Compute SHA256 for the requested file
        printf("<Server>Worker %ld: computing SHA256 for %s\n",
               pthread_self(), req->pathname);

        uint8_t hash[32];
        digest_file(req->pathname, hash);

        char char_hash[65];
        for (int i = 0; i < 32; i++)
            sprintf(char_hash + (i * 2), "%02x", hash[i]);

        // Prepare the response for the clients
        struct Response response;
        strcpy(response.hash, char_hash);

        // Send the response to all waiting clients
        client_node_t *clients = req->clients;
        while (clients)
        {
            sendResponse(&response, clients->pid);
            client_node_t *tmp = clients;
            clients = clients->next;
            free(tmp);
        }

        free(req);
    }
    return NULL;
}

// Handles server termination: closes the FIFO descriptors, removes the FIFO, and exits the process
void quit(int sig)
{
    // Close the FIFO descriptors
    if (serverFIFO != 0 && close(serverFIFO) == -1)
        errExit("close failed");

    if (serverFIFO_extra != 0 && close(serverFIFO_extra) == -1)
        errExit("close failed");

    // Remove the FIFO from the filesystem
    if (unlink(path2ServerFIFO) != 0)
        errExit("unlink failed");

    // Terminate the process
    _exit(0);
}

// Computes the SHA256 hash of a file and writes it to the hash array
void digest_file(const char *filename, uint8_t *hash)
{
    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    char buffer[32];

    int file = open(filename, O_RDONLY, 0);
    if (file == -1)
    {
        printf("File %s does not exist\n", filename);
        exit(1);
    }

    ssize_t bR;
    do
    {
        // read the file in chunks of 32 characters
        bR = read(file, buffer, 32);
        if (bR > 0)
        {
            SHA256_Update(&ctx, (uint8_t *)buffer, bR);
        }
        else if (bR < 0)
        {
            printf("Read failed\n");
            exit(1);
        }
    } while (bR > 0);

    SHA256_Final(hash, &ctx);

    close(file);
}

// Sends a Response to a client through its FIFO
void sendResponse(struct Response *response, pid_t cPid)
{
    // Build the path to the client's FIFO
    char path2ClientFIFO[50];
    sprintf(path2ClientFIFO, "%s%d", baseClientFIFO, cPid);

    printf("<Server> opening FIFO %s...\n", path2ClientFIFO);
    // Open the client's FIFO in write-only mode
    int clientFIFO = open(path2ClientFIFO, O_WRONLY);
    if (clientFIFO == -1)
        errExit("open: failed to open client FIFO");

    printf("<Server> Sending a response to client PID %d\n", cPid);
    // Write the Response into the opened FIFO
    if (write(clientFIFO, response, sizeof(struct Response)) != sizeof(struct Response))
        errExit("write: failed to write to client FIFO");

    // Close the FIFO
    if (close(clientFIFO) == -1)
        errExit("close: failed to close client FIFO");
}

int main(int argc, char *argv[])
{
    printf("<Server> Creating the server FIFO...\n");
    // Create the FIFO with the following permissions:
    // user: read, write; group: write; other: no permission
    if (mkfifo(path2ServerFIFO, S_IRUSR | S_IWUSR | S_IWGRP) == -1)
        errExit("mkfifo: failed to create server FIFO");

    printf("<Server> FIFO %s created!\n", path2ServerFIFO);

    // Set a signal handler for SIGINT to perform cleanup
    signal(SIGINT, quit);

    // Wait for clients: open the server FIFO in read-only mode
    printf("<Server> Waiting for a client connection...\n");
    serverFIFO = open(path2ServerFIFO, O_RDONLY);
    if (serverFIFO == -1)
        errExit("open: failed to open server FIFO for reading");

    // Open an extra write descriptor to prevent EOF when all clients disconnect
    serverFIFO_extra = open(path2ServerFIFO, O_WRONLY);
    if (serverFIFO_extra == -1)
        errExit("open: failed to open extra write descriptor for server FIFO");

    // Calculate the thread pool size based on available CPU cores ( -1 for the thread manager)
    long thread_pool_size = sysconf(_SC_NPROCESSORS_ONLN) - 1;
    if (thread_pool_size < 0)
        errExit("sysconf: failed to get the number of available processors");

    // Create the thread pool
    pthread_t threads[thread_pool_size];
    for (int i = 0; i < thread_pool_size; i++)
    {
        if (pthread_create(&threads[i], NULL, worker_thread, NULL) != 0)
            errExit("pthread_create: failed to create worker thread");
    }

    // Read requests from the FIFO and update the request list for worker threads
    struct Request request;
    int bR = -1;
    do
    {
        printf("<Server> Waiting for a request from a client...\n");
        // Read a request from the FIFO
        bR = read(serverFIFO, &request, sizeof(struct Request));

        // Check the number of bytes read from the FIFO
        if (bR == -1)
        {
            printf("<Server> it looks like the FIFO is broken\n");
        }
        else if (bR != sizeof(struct Request))
            printf("<Server> it looks like I did not receive a valid request\n");
        else
            update_request_list(&request);

    } while (bR != -1);

    // The FIFO is broken, run quit() to remove the FIFO and terminate the process
    quit(0);
}