#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <openssl/sha.h>

#include "errExit.h"
#include "request_response.h"

// FIFO path to handle the sha requests
char *path2ServerFIFO = "/tmp/fifo_server_SHA256";
char *baseClientFIFO = "/tmp/fifo_client_SHA256."; // to complete with the processID

// FIFO's file descriptor
int serverFIFO, serverFIFO_extra;

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

void sendResponse(struct Request *request)
{

    // make the path of client's FIFO
    char path2ClientFIFO[25];
    sprintf(path2ClientFIFO, "%s%d", baseClientFIFO, request->cPid);

    printf("<Server> opening FIFO %s...\n", path2ClientFIFO);
    // Open the client's FIFO in write-only mode
    int clientFIFO = open(path2ClientFIFO, O_WRONLY);
    if (clientFIFO == -1)
        errExit("\n Error while opening the client_fifo\n");

    // Preparing the hash256
    uint8_t hash[32];
    digest_file(request->pathname, hash);

    char char_hash[65];
    for (int i = 0; i < 32; i++)
        sprintf(char_hash + (i * 2), "%02x", hash[i]);

    // DEBUGGG  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!1
    printf("Sending %s\n", char_hash);

    // Prepare the response for the client
    struct Response response;
    strcpy(response.hash, char_hash);

    printf("<Server> sending a response\n");
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
        {

            sendResponse(&request);
        }

    } while (bR != -1);

    // the FIFO is broken, run quit() to remove the FIFO and
    // terminate the process.
    quit(0);
}