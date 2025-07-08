#include "errExit.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

void errExit(const char *msg)
{
    if (errno != 0)
        perror(msg);

    else // avoid : success
        fprintf(stderr, "%s", msg);

    exit(EXIT_FAILURE);
}