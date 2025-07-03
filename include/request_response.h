#ifndef _REQUEST_RESPONSE_HH
#define _REQUEST_RESPONSE_HH

#include <sys/types.h>

struct Request
{                       /* Request (client --> server) */
    pid_t cPid;         /* PID of client               */
    char pathname[100]; /* pathname of the file        */
};

struct Response
{                  /* Response (server --> client) */
    char hash[65]; /* sha256                       */
};

#endif
