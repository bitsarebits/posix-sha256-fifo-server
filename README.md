# SHA256-FIFO-Server

Small POSIX server/client project that computes the SHA-256 digest of files on request.

- Communication via named pipes (FIFOs)
- Server uses a master thread and a worker thread pool (pthreads)
- Duplicate requests for the same file are aggregated and served together
- Results are cached to avoid recomputation when the file has not changed
- Client sends a pathname and receives the SHA-256 hex string

## Build

```bash
mkdir build && cd build
cmake ..
make
```

Requires OpenSSL development headers.

## Usage

Start the server (creates `/tmp/fifo_server_SHA256`):

```bash
./server
```

In another shell run a client:

```bash
./client /path/to/file
```

The client creates a FIFO `/tmp/fifo_client_SHA256.<PID>` and receives a `Response` struct containing the hash (or an error code).

## Example output

```
$ ./client test.bin
SHA-256(/home/user/test.bin) = 7f83b1657ff1fc53b92dc18148a1d65dfc2d4b1fa3d677284addd200126d9069
```

## Files

- `src/server.c` — server implementation
- `src/client.c` — client implementation
- `src/request_response.c` — error helper
- `src/errExit.c` — error exit helper
- `include/request_response.h` — shared structs and error codes

## Documentation

See [docs/Architecture.md](docs/Architecture.md) for details on design, synchronization, and caching.

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.
