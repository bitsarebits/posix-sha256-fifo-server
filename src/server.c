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

#define CACHE_SIZE 1024

// FIFO path for handling SHA256 requests from clients
char *path2ServerFIFO = "/tmp/fifo_server_SHA256";
char *baseClientFIFO = "/tmp/fifo_client_SHA256."; // completed with the process ID

// FIFO file descriptors
int serverFIFO = -1;
int serverFIFO_extra = -1;

// Node for the list of clients waiting for the same file hash
typedef struct client_node
{
    pid_t pid;
    struct client_node *next;
} client_node_t;

// Node for the request list
typedef struct request_list
{
    short errCode;
    char pathname[PATH_MAX];
    time_t last_mod_time;
    size_t filesize;
    client_node_t *clients;
    struct request_list *next;
} request_list_t;

// Node for the cache table
typedef struct cache_entry
{
    char pathname[PATH_MAX];
    time_t last_mod_time;
    uint8_t sha256[32];       // hash SHA256 32 bytes
    struct cache_entry *next; // Next entry in case of collision
} cache_entry_t;

// Initialize the requests list head, the mutex, and the condition variable for thread synchronization
request_list_t *request_list_head = NULL;
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t list_cond = PTHREAD_COND_INITIALIZER;

// Initialize the cache table and the mutex
cache_entry_t *cache[CACHE_SIZE] = {NULL};
pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

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
 * Send a response calling fifo_client() for each client in req->clients.
 * Frees the request_list element and its client list memory after sending.
 */
void send_response(request_list_t *req, struct Response *response);

/**
 * Frees all memory allocated for the hash cache.
 * Called during server termination.
 */
void cache_cleanup(void);

/**
 * Handles server termination: closes and removes the FIFO, terminates the process.
 */
void quit(int sig);

/**
 * Wrapper function for atexit to ensure cleanup on normal process termination.
 * Calls quit with a default signal value.
 */
void quit_atexit(void);

/**
 * Computes the SHA256 hash of a file and writes it to the hash array.
 * Returns an Error Code from request_response.h if something goes wrong, 0 on success.
 */
short digest_file(const char *filename, uint8_t *hash);

/**
 * Sends a Response to the client through its FIFO.
 */
void fifo_client(struct Response *response, pid_t cPid);

/**
 * Computes a hash value for a given pathname and mtime using the djb2 algorithm.
 */
unsigned int hash_path(const char *path, time_t mtime);

/**
 * Searches the cache for a previously computed SHA256.
 * Returns pointer to cache entry or NULL if not found.
 */
cache_entry_t *cache_lookup(const char *pathname, time_t mtime);

/**
 * Inserts a new SHA256 hash into the cache.
 */
void cache_insert(const char *pathname, time_t mtime, const uint8_t *sha256);

// Add a new request to the request list
void update_request_list(struct Request *request)
{
    // Read file stats to get the last modification time
    struct stat st;
    if (stat(request->pathname, &st) != 0)
    {
        // Lock the mutex to synchronize with other threads
        pthread_mutex_lock(&list_mutex);

        // Add an invalid element to the list with errCode STAT_E
        request_list_t *new_req = malloc(sizeof(request_list_t));
        if (!new_req)
        {
            printf("<Server> Malloc failed, client %d not served", request->cPid);
            return;
        }
        new_req->errCode = STAT_E; // stat failed
        strncpy(new_req->pathname, request->pathname, PATH_MAX);
        new_req->clients = malloc(sizeof(client_node_t));
        if (!new_req->clients)
        {
            printf("<Server> Malloc failed, client %d not served", request->cPid);
            return;
        }
        new_req->clients->pid = request->cPid;
        new_req->clients->next = NULL;

        // Insert the invalid request into the list, the worker will handle this
        new_req->next = request_list_head;
        request_list_head = new_req;
        printf("<Server> Stat failed for %s\n", request->pathname);

        // Release the mutex and return
        pthread_mutex_unlock(&list_mutex);
        return;
    }

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
            if (!new_client)
            {
                printf("<Server> Malloc failed, client %d not served", request->cPid);
                return;
            }
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
    if (!new_req)
    {
        printf("<Server> Malloc failed, client %d not served", request->cPid);
        return;
    }
    new_req->errCode = 0; // success
    strncpy(new_req->pathname, request->pathname, PATH_MAX);
    new_req->last_mod_time = st.st_mtime;
    new_req->filesize = st.st_size;
    new_req->clients = malloc(sizeof(client_node_t));
    if (!new_req->clients)
    {
        printf("<Server> Malloc failed, client %d not served", request->cPid);
        return;
    }
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

// Worker thread: handles client requests; waits on a condition variable if the list is empty;
// uses cache to avoid recomputing SHA256
void *worker_thread(void *arg)
{
    sleep(20);
    while (1)
    {
        // Acquire the list_mutex to access the request list
        pthread_mutex_lock(&list_mutex);

        // If the list is empty, wait on the condition variable
        while (!request_list_head)
            pthread_cond_wait(&list_cond, &list_mutex);

        // take a request from the head of the list
        request_list_t *req = request_list_head;
        request_list_head = request_list_head->next;

        // Unlock the list_mutex
        pthread_mutex_unlock(&list_mutex);

        struct Response response;
        if (req->errCode != 0)
        {
            response.errCode = req->errCode;
            send_response(req, &response);
            continue;
        }

        // Compute SHA256 for the requested file
        printf("<Server> Worker %ld: computing SHA256 for %s\n",
               pthread_self(), req->pathname);

        // Initialize to zeros
        uint8_t hash[32] = {0};

        // Check if SHA256 is already cached
        pthread_mutex_lock(&cache_mutex);
        cache_entry_t *cached = cache_lookup(req->pathname, req->last_mod_time);
        pthread_mutex_unlock(&cache_mutex);

        if (cached)
        {
            // Cache HIT: reuse cached SHA256
            printf("<Server> Worker %ld: cache HIT for %s\n", pthread_self(), req->pathname);
            memcpy(hash, cached->sha256, 32);
        }
        else
        {
            // Cache MISS: compute SHA256 and insert into cache
            printf("<Server> Worker %ld: cache MISS for %s, computing SHA256...\n", pthread_self(), req->pathname);

            response.errCode = digest_file(req->pathname, hash);

            if (response.errCode != 0 && response.errCode != CLOSE_FILE_E)
            {
                send_response(req, &response);
                continue;
            }
            cache_insert(req->pathname, req->last_mod_time, hash);
        }

        // Convert binary SHA256 to hex string
        char char_hash[65];
        for (int i = 0; i < 32; i++)
            sprintf(char_hash + (i * 2), "%02x", hash[i]);

        // Prepare the response for the clients
        strcpy(response.hash, char_hash);

        // Send the response to all waiting clients
        send_response(req, &response);
    }
    return NULL;
}

// Send the response to all waiting clients
void send_response(request_list_t *req, struct Response *response)
{
    client_node_t *clients = req->clients;
    while (clients)
    {
        fifo_client(response, clients->pid);
        client_node_t *tmp = clients;
        clients = clients->next;
        free(tmp);
    }

    free(req);
}

// Cleanup the cache and free the memory allocated
void cache_cleanup()
{
    pthread_mutex_lock(&cache_mutex);
    for (int i = 0; i < CACHE_SIZE; i++)
    {
        cache_entry_t *entry = cache[i];
        while (entry)
        {
            cache_entry_t *next = entry->next;
            free(entry);
            entry = next;
        }
        cache[i] = NULL;
    }
    pthread_mutex_unlock(&cache_mutex);
}

// Handles server termination: closes the FIFO descriptors, removes the FIFO, and exits the process
void quit(int sig)
{
    // cleanup the cache
    printf("<Server> Cleanup the cache\n");
    cache_cleanup();

    // Close the FIFO descriptors
    if (serverFIFO != -1 && close(serverFIFO) == -1)
        errExit("close failed");

    if (serverFIFO_extra != -1 && close(serverFIFO_extra) == -1)
        errExit("close failed");

    // Remove FIFO from the filesystem (ignore errors if already removed)
    unlink(path2ServerFIFO);
    printf("<Server> %s closed and removed from the filesystem\n", path2ServerFIFO);

    // Terminate the process
    _exit(0);
}

// Calls quit with a default signal value
void quit_atexit(void) { quit(SIGINT); }

// Computes the SHA256 hash of a file and writes it to the hash array
short digest_file(const char *filename, uint8_t *hash)
{
    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    char buffer[32];

    int file = open(filename, O_RDONLY, 0);
    if (file == -1)
    {
        printf("<Server> Worker %ld: Can't open the file %s\n", pthread_self(), filename);
        return OPEN_FILE_E;
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
            printf("<Server> Worker %ld: Can't read the file\n", pthread_self());
            return READ_FILE_E;
        }
    } while (bR > 0);

    SHA256_Final(hash, &ctx);

    if (close(file) != 0)
    {
        printf("<Server> close failed for %s", filename);
        return CLOSE_FILE_E;
    }
    return 0;
}

// Sends a Response to a client through its FIFO
void fifo_client(struct Response *response, pid_t cPid)
{
    // Build the path to the client's FIFO
    char path2ClientFIFO[50];
    sprintf(path2ClientFIFO, "%s%d", baseClientFIFO, cPid);

    printf("<Server> Worker %ld: Sending a response to client PID %d...\n", pthread_self(), cPid);
    // Open the client's FIFO in write-only mode
    int clientFIFO = open(path2ClientFIFO, O_WRONLY);
    if (clientFIFO == -1)
    {
        printf("<Server> Worker %ld: failed to open client FIFO %s", pthread_self(), path2ClientFIFO);
        return;
    }

    // Write the Response into the opened FIFO
    if (write(clientFIFO, response, sizeof(struct Response)) != sizeof(struct Response))
    {
        printf("<Server> Worker %ld: failed to write on client FIFO %s", pthread_self(), path2ClientFIFO);
    }

    // Close the FIFO
    if (close(clientFIFO) == -1)
    {
        printf("<Server> Worker %ld: failed to close client FIFO %s", pthread_self(), path2ClientFIFO);
        return;
    }
}

// djb2 hash function for pathname and mtime
unsigned int hash_path(const char *path, time_t mtime)
{
    unsigned int hash = 5381;
    int c;

    // Process each character of the pathname
    while ((c = *path++))
        hash = ((hash << 5) + hash) + c; // hash * 33 + c

    // Mix last modification time
    hash = ((hash << 5) + hash) + (unsigned int)mtime;

    return hash % CACHE_SIZE; // Keep index in range
}

// Searches the cache for a previously computed SHA256
// Returns pointer to cache entry or NULL if not found
cache_entry_t *cache_lookup(const char *pathname, time_t mtime)
{
    // calculate the hash table entry
    unsigned int idx = hash_path(pathname, mtime);

    cache_entry_t *entry = cache[idx];
    while (entry)
    {
        if (strcmp(entry->pathname, pathname) == 0 &&
            entry->last_mod_time == mtime)
            return entry; // cache HIT
        entry = entry->next;
    }
    return NULL; // cache MISS
}

// Inserts a new SHA256 hash into the cache
// Adds entry to head of chain for this bucket
void cache_insert(const char *pathname, time_t mtime, const uint8_t *sha256)
{
    // Hash table index
    unsigned int index = hash_path(pathname, mtime);

    // New cache entry
    cache_entry_t *new_entry = malloc(sizeof(cache_entry_t));
    if (!new_entry)
    {
        printf("<Server> Worker %ld: Malloc failed, %s not stored in the cache\n", pthread_self(), pathname);
        return;
    }

    strncpy(new_entry->pathname, pathname, PATH_MAX - 1);
    new_entry->pathname[PATH_MAX - 1] = '\0';
    new_entry->last_mod_time = mtime;
    memcpy(new_entry->sha256, sha256, 32);

    // Insert at head of collision chain, use the mutex for thread synchronization
    pthread_mutex_lock(&cache_mutex);
    new_entry->next = cache[index];
    cache[index] = new_entry;
    pthread_mutex_unlock(&cache_mutex);
}

int main(int argc, char *argv[])
{
    printf("<Server> Creating the server FIFO...\n");
    // Create the FIFO with the following permissions:
    // user: read, write; group: write; other: no permission
    if (mkfifo(path2ServerFIFO, S_IRUSR | S_IWUSR | S_IWGRP) == -1)
        errExit("mkfifo: failed to create server FIFO");

    printf("<Server> FIFO %s created!\n", path2ServerFIFO);

    // Set a signal handler for SIGINT and atexit to perform cleanup
    signal(SIGINT, quit);
    atexit(quit_atexit);

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
    if (thread_pool_size < 1)
        thread_pool_size = 1; // Minimum 1 worker thread

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
        {
            printf("<Server> Received %s from %d\n", request.pathname, request.cPid);
            update_request_list(&request);
        }

    } while (bR != -1);

    // The FIFO is broken, run quit() to remove the FIFO and terminate the process
    quit(0);
}