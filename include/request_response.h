#ifndef REQUEST_RESPONSE_H
#define REQUEST_RESPONSE_H

#include <sys/types.h>
#include <limits.h>

#ifndef PATH_MAX
/* Fallback if PATH_MAX is not provided by the platform headers */
#define PATH_MAX 4096
#endif

// Error codes
#define STAT_FILE_E -1
#define OPEN_FILE_E -2
#define READ_FILE_E -3
#define CLOSE_FILE_E -4

// Struct mapping error codes to messages
typedef struct
{
    short code;
    const char *message;
} error_entry_t;

// Table with error messages
static const error_entry_t error_table[] = {
    {STAT_FILE_E, "Error: The server failed to retrieve file statistics\n"},
    {OPEN_FILE_E, "Error: The server couldn't open the file\n"},
    {READ_FILE_E, "Error: The server couldn't read the file\n"},
    {CLOSE_FILE_E, "Error: The server couldn't close the file\n"},
};

/**
 * Retrieves the error message corresponding to a given error code.
 */
const char *get_error_message(int code);

// Structure representing a request sent from client to server
struct Request
{
    pid_t cPid;              // PID of the client sending the request
    char pathname[PATH_MAX]; // Pathname of the file
};

// Structure representing a response sent from server to client
struct Response
{
    short errCode; // Error code indicating success or failure
    char hash[65]; // SHA-256 hash string (64 hex digits + null terminator)
};

#endif
