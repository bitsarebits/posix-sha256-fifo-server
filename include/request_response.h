#ifndef _REQUEST_RESPONSE_HH
#define _REQUEST_RESPONSE_HH

#include <sys/types.h>

#define PATH_MAX 512

struct Request
{                            /* Request (client --> server) */
    pid_t cPid;              /* PID of client               */
    char pathname[PATH_MAX]; /* pathname of the file        */
};

struct Response
{                  /* Response (server --> client) */
    char hash[65]; /* sha256                       */
};

#endif
