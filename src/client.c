#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "request_response.h"
#include "errExit.h"

// FIFO path to handle the sha requests
char *path2ServerFIFO = "/tmp/fifo_server_SHA256";
char *baseClientFIFO = "/tmp/fifo_client_SHA256."; // to complete with the processID

#define MAX 100

int main(int argc, char *argv[])
{

    // Check command line input arguments
    // The program only wants a pathname of the file to convert with sha256
    if (argc != 2)
    {
        printf("Usage: %s pathname\n", argv[0]);
        return 0;
    }

    // makes a FIFO in /tmp
    char path2ClientFIFO[25];
    sprintf(path2ClientFIFO, "%s%d", baseClientFIFO, getpid());

    printf("<Client> making FIFO...\n");
    // make a FIFO with the following permissions:
    // user:  read, write
    // group: write
    // other: no permission
    if (mkfifo(path2ClientFIFO, S_IRUSR | S_IWUSR | S_IWGRP) == -1)
        errExit("\nError while creating the client_fifo.pid\n");

    printf("<Client> FIFO %s created!\n", path2ClientFIFO);

    // open the server's FIFO to send a Request
    printf("<Client> opening FIFO %s...\n", path2ServerFIFO);
    int serverFIFO = open(path2ServerFIFO, O_WRONLY);
    if (serverFIFO == -1)
        errExit("\nError while opening the server_fifo\n");

    // Prepare a request
    struct Request request;
    request.cPid = getpid();
    strncpy(request.pathname, argv[1], sizeof(request.pathname) - 1);
    request.pathname[sizeof(request.pathname) - 1] = '\0';

    // send a Request through the server's FIFO
    printf("<Client> sending %s\n", request.pathname);
    if (write(serverFIFO, &request, sizeof(request)) != sizeof(struct Request))
        errExit("\nError while writing the Request on the server_fifo\n");

    // opens the FIFO.pid to get a Response
    int clientFIFO = open(path2ClientFIFO, O_RDONLY);
    if (clientFIFO == -1)
        errExit("\nError while opening the client_fifo.pid\n");

    // read a Response from the server
    struct Response response;
    if (read(clientFIFO, &response, sizeof(struct Response)) != sizeof(struct Response))
        errExit("\nError while reading the response from client_fifo.pid\n");

    // Step-6: The client prints the result on terminal
    printf("<Client> The SHA256 is: %s\n", response.hash);

    // close the FIFO
    if (close(clientFIFO) == -1)
        errExit("\nError while closing the client_fifo\n");

    // remove the FIFO from the file system
    if (unlink(path2ClientFIFO) == -1)
        errExit("\nError while unlinking the client_fifo.pid\n");
}