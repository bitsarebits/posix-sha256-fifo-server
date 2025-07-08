#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include "request_response.h"
#include "errExit.h"

// Function prototypes for cleanup
/**
 * Handles client termination: removes the client FIFO and exits the process.
 */
void quit(int sig);

/**
 * Wrapper function for atexit to ensure cleanup on normal process termination.
 * Calls quit with a default signal value.
 */
void quit_atexit(void);

// FIFO paths for handling SHA256 requests
char *path2ServerFIFO = "/tmp/fifo_server_SHA256";
char *baseClientFIFO = "/tmp/fifo_client_SHA256."; // completed with the process ID

#define MAX 100

int main(int argc, char *argv[])
{
    // Check command line arguments: expects a single pathname
    if (argc != 2)
    {
        printf("Usage: %s <pathname>\n", argv[0]);
        return 0;
    }

    // Register cleanup functions for SIGINT and normal exit
    signal(SIGINT, quit);
    atexit(quit_atexit);

    // Create the client FIFO in /tmp
    char path2ClientFIFO[50];
    sprintf(path2ClientFIFO, "%s%d", baseClientFIFO, getpid());

    printf("<Client> Creating FIFO %s...\n", path2ClientFIFO);
    // // Create the FIFO with the following permissions:
    // user: read, write; group: write; other: no permission
    if (mkfifo(path2ClientFIFO, S_IRUSR | S_IWUSR | S_IWGRP) == -1)
        errExit("mkfifo: failed to create client FIFO");

    printf("<Client> FIFO %s created!\n", path2ClientFIFO);

    // Open the server FIFO to send a request
    printf("<Client> Opening server FIFO %s...\n", path2ServerFIFO);
    int serverFIFO = open(path2ServerFIFO, O_WRONLY);
    if (serverFIFO == -1)
        errExit("open: failed to open server FIFO");

    // Prepare the request
    struct Request request;
    request.cPid = getpid();
    strncpy(request.pathname, argv[1], sizeof(request.pathname) - 1);
    request.pathname[sizeof(request.pathname) - 1] = '\0';

    // Send the request through the server FIFO
    printf("<Client> Sending request for file: %s\n", request.pathname);
    if (write(serverFIFO, &request, sizeof(request)) != sizeof(struct Request))
        errExit("write: failed to write request to server FIFO");

    // Open the client FIFO to receive the response
    printf("<Client> Opening client FIFO %s...\n", path2ClientFIFO);
    int clientFIFO = open(path2ClientFIFO, O_RDONLY);
    if (clientFIFO == -1)
        errExit("open: failed to open client FIFO");

    // Read the response from the server
    struct Response response;
    if (read(clientFIFO, &response, sizeof(struct Response)) != sizeof(struct Response))
        errExit("read: failed to read response from client FIFO");

    if (response.errCode != 0 && response.errCode != CLOSE_FILE_E)
        errExit(get_error_message(response.errCode));

    // Print the result
    printf("<Client> The SHA256 is: %s\n", response.hash);

    if (response.errCode == CLOSE_FILE_E)
        fprintf(stderr, "%s", get_error_message(response.errCode));

    // Close the client FIFO
    if (close(clientFIFO) == -1)
        errExit("close: failed to close client FIFO");

    // Remove the client FIFO from the file system
    if (unlink(path2ClientFIFO) == -1)
        errExit("unlink: failed to remove client FIFO");

    printf("<Client> %s closed and removed from the filesystem\n", path2ClientFIFO);

    return 0;
}

// Handles client termination: removes the client FIFO and exits the process.
void quit(int sig)
{
    // Remove the client FIFO from the file system (ignore errors if it does not exist)
    char path2ClientFIFO[50];
    sprintf(path2ClientFIFO, "%s%d", baseClientFIFO, getpid());
    printf("<Client> Closing the %s", path2ClientFIFO);
    unlink(path2ClientFIFO);

    // Terminate the process
    _exit(0);
}

// Calls quit with a default signal value.
void quit_atexit(void) { quit(SIGINT); }