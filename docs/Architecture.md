# Architecture

This document describes the internal design of **SHA256-FIFO-Server**.

## Overview

The system is composed of a **server** and multiple **clients** communicating via POSIX FIFOs. The client sends the pathname of a file and receives the SHA-256 digest computed by the server.

- **Server FIFO**: `/tmp/fifo_server_SHA256`
- **Client FIFO**: `/tmp/fifo_client_SHA256.<PID>`

Requests are aggregated: if multiple clients request the same file, the server computes the hash only once and replies to all of them.

A cache stores results for files not modified since the last computation.

## Threads

### Master Thread

- Opens the server FIFO and reads `Request` structures.
- For each request:

  - Checks if the file is already in `in_progress` (a worker is computing it).
  - If yes: adds the client PID to the list of waiting clients.
  - If not: inserts a new entry in the `pending` list (ordered by file size).

- Signals worker threads via a condition variable.

### Worker Threads

- Wait on a condition variable until a new request is available.
- Move the request from `pending` to `in_progress`.
- If the request has an error code (e.g., `stat` failed), send an error response immediately.
- Otherwise:

  - Check the cache for a valid SHA-256.
  - If found, return it.
  - If not, compute it, insert it into the cache, then return it.

- Send the response to all clients waiting for that file.

### Synchronization

- **list_mutex**: protects `pending` and `in_progress` lists.
- **cache_mutex**: protects the cache table.
- **stats_mutex**: protects global counters (clients served, cache hits/misses).
- **list_cond**: condition variable used to wake workers.

Atomicity of FIFO writes is guaranteed because `struct Request` and `struct Response` are smaller than `PIPE_BUF`.

## Data Structures

### Request

```c
struct Request {
    pid_t cPid;              // Client PID
    char pathname[PATH_MAX]; // File path
};
```

### Response

```c
struct Response {
    short errCode; // Error code (0 if success)
    char hash[65]; // 64 hex chars + null terminator
};
```

### Request List Node

```c
typedef struct request_list {
    short errCode;
    char pathname[PATH_MAX];
    time_t last_mod_time;
    size_t filesize;
    client_node_t *clients;
    struct request_list *next;
} request_list_t;
```

### Cache Entry

```c
typedef struct cache_entry {
    char pathname[PATH_MAX];
    time_t last_mod_time;
    uint8_t sha256[32];
    struct cache_entry *next;
} cache_entry_t;
```

Cache is implemented as a hash table with chaining for collisions.

## Shutdown

- A global atomic flag `server_running` controls termination.
- `quit()` function:

  - Sets `server_running = false`.
  - Broadcasts on the condition variable to wake all workers.
  - Joins all worker threads.
  - Cleans up memory, cache, and FIFOs.
  - Registered as SIGINT handler and with `atexit()`.

## Error Handling

- Non-critical errors are propagated via `Response.errCode`.
- Error codes are defined in `request_response.h` and mapped to human-readable messages.
- Examples:

  - `STAT_FILE_E`: cannot `stat` file → respond with error.
  - `CLOSE_FILE_E`: failure closing file → respond with hash + error code.

## Statistics

The server collects:

- Total clients served
- SHA-256 computed per worker
- Cache hits and misses
- Hit rate (hits / total requests)

Values are displayed at shutdown for
