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

// FIFO path to handle the sha requests
char *path2ServerFIFO = "/tmp/fifo_server_SHA256";
char *baseClientFIFO = "/tmp/fifo_client_SHA256."; // to complete with the processID

// FIFO's file descriptor
int serverFIFO, serverFIFO_extra;

// Struct to handle the requests list
// list of pid for multiple requests of the same pathname
typedef struct client_node
{
    pid_t pid;
    struct client_node *next;
} client_node_t;

// List to manage requests. Threads read requests from the list and process them
typedef struct request_list
{
    char pathname[PATH_MAX];
    time_t last_mod_time;
    size_t filesize;
    client_node_t *clients;
    struct request_list *next;
} request_list_t;

// request list, a mutex, and a condition variable to synchronize the threads
request_list_t *request_list_head = NULL;
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t list_cond = PTHREAD_COND_INITIALIZER;

// Add a new request to the request list
void update_request_list(struct Request *request)
{
    // read the file stats to get the last modification time
    struct stat st;
    if (stat(request->pathname, &st) != 0)
        errExit("stat failed\n");

    // mutex to synchronize with other threads
    pthread_mutex_lock(&list_mutex);

    request_list_t *prev = NULL, *curr = request_list_head;

    while (curr)
    {
        if (strcmp(curr->pathname, request->pathname) == 0 &&
            curr->last_mod_time == st.st_mtime)
        {
            // Path-mtime already in the list, add the client pid
            // only one thread will calculate the sha256 and send to multiple clients
            client_node_t *new_client = malloc(sizeof(client_node_t));
            new_client->pid = request->cPid;
            new_client->next = curr->clients;
            curr->clients = new_client;

            // the update function releases the mutex and terminates
            pthread_mutex_unlock(&list_mutex);
            return;
        }
        if (st.st_size < curr->filesize)
            break;
        prev = curr;
        curr = curr->next;
    }

    // New Request
    request_list_t *new_req = malloc(sizeof(request_list_t));
    strncpy(new_req->pathname, request->pathname, PATH_MAX);
    new_req->last_mod_time = st.st_mtime;
    new_req->filesize = st.st_size;
    new_req->clients = malloc(sizeof(client_node_t));
    new_req->clients->pid = request->cPid;
    new_req->clients->next = NULL;

    // Insert the request in the list
    new_req->next = curr;
    if (prev)
        prev->next = new_req;
    else
        request_list_head = new_req;

    // wake up a worker thread and release the mutex
    pthread_cond_signal(&list_cond);
    pthread_mutex_unlock(&list_mutex);
}

// Handle client requests; wait on a condition variable if the list is empty
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

        // Read a request from the list
        request_list_t *req = request_list_head;
        request_list_head = request_list_head->next;

        // Unlock the mutex
        pthread_mutex_unlock(&list_mutex);

        // Calcola SHA256 (placeholder)
        printf("<Server>Worker %ld: computing SHA256 for %s\n",
               pthread_self(), req->pathname);

        // Preparing the hash256
        uint8_t hash[32];
        digest_file(req->pathname, hash);

        char char_hash[65];
        for (int i = 0; i < 32; i++)
            sprintf(char_hash + (i * 2), "%02x", hash[i]);

        // Prepare the response for the clients
        struct Response response;
        strcpy(response.hash, char_hash);

        // Send response to all the clients
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

// the quit function closes the file descriptors for the FIFO,
// removes the FIFO from the file system, and terminates the process
void quit(int sig)
{
    // Close the FIFO
    if (serverFIFO != 0 && close(serverFIFO) == -1)
        errExit("close failed");

    if (serverFIFO_extra != 0 && close(serverFIFO_extra) == -1)
        errExit("close failed");

    // Remove the FIFO
    if (unlink(path2ServerFIFO) != 0)
        errExit("unlink failed");

    // terminate the process
    _exit(0);
}

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

// Send a Response to a client
void sendResponse(struct Response *response, pid_t cPid)
{

    // make the path of client's FIFO
    char path2ClientFIFO[25];
    sprintf(path2ClientFIFO, "%s%d", baseClientFIFO, cPid);

    printf("<Server> opening FIFO %s...\n", path2ClientFIFO);
    // Open the client's FIFO in write-only mode
    int clientFIFO = open(path2ClientFIFO, O_WRONLY);
    if (clientFIFO == -1)
        errExit("\n Error while opening the client_fifo\n");

    printf("<Server> Sending a response to client PID %d\n", cPid);
    // Write the Response into the opened FIFO
    if (write(clientFIFO, &response, sizeof(response)) != sizeof(struct Response))
        errExit("\nError while writing in the client_fifo\n");

    // Close the FIFO
    if (close(clientFIFO) == -1)
        errExit("\nError while closing the client_fifo\n");
}

int main(int argc, char *argv[])
{

    printf("<Server> Making the server FIFO...\n");
    // make a FIFO with the following permissions:
    // user:  read, write
    // group: write
    // other: no permission
    if (mkfifo(path2ServerFIFO, S_IRUSR | S_IWUSR | S_IWGRP) == -1)
        errExit("\nError while creating the fifo_server\n");

    printf("<Server> FIFO %s created!\n", path2ServerFIFO);

    // set a signal handler for SIGINT signals
    signal(SIGINT, quit);

    // Wait for client in read-only mode
    printf("<Server> waiting for a client...\n");
    serverFIFO = open(path2ServerFIFO, O_RDONLY);
    if (serverFIFO == -1)
        errExit("\nError while opening the fifo_server\n");

    // Open an extra descriptor, so that the server does not see end-of-file
    // even if all clients closed the write end of the FIFO
    serverFIFO_extra = open(path2ServerFIFO, O_WRONLY);
    if (serverFIFO_extra == -1)
        errExit("open write-only extra failed");

    // Calculate the thread_pool_size based on the cores available
    long thread_pool_size = sysconf(_SC_NPROCESSORS_ONLN) - 1; // -1 for the thread manager
    if (thread_pool_size < 0)
        errExit("Sysconf failed\n");

    // Create the thread pool
    pthread_t threads[thread_pool_size];
    for (int i = 0; i < thread_pool_size; i++)
    {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

    // Read from the FIFO and update the requests list for the worker threads
    struct Request request;
    int bR = -1;
    do
    {
        printf("<Server> waiting for a Request...\n");
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

    // the FIFO is broken, run quit() to remove the FIFO and
    // terminate the process.
    quit(0);
}