#include "request_response.h"

const size_t table_size = sizeof(error_table) / sizeof(error_table[0]);

const char *get_error_message(int code)
{
    for (size_t i = 0; i < table_size; i++)
    {
        if (error_table[i].code == code)
        {
            return error_table[i].message;
        }
    }
    return "Unknown error code\n";
}