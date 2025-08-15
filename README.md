# RDMA Key-Value Store

This project provides a minimal key-value store built on top of Remote Direct Memory Access (RDMA). It demonstrates how a client and server can communicate using RDMA verbs for low-latency data access.

## Building

Ensure the RDMA development libraries are installed on your system. Then compile the project with:

```sh
make
```

This produces an `application` binary and a `client` symlink to it.

## Usage

Start the server:

```sh
./application
```

Run the client, supplying the server's hostname or IP address:

```sh
./client <server>
```

The client sends requests to the server, which manages key-value pairs using RDMA.

## API

### Key-Value Interface

- `int kv_open(char *servername, void **kv_handle);` Connect to the server and return a handle for subsequent operations.
- `int kv_set(void *kv_handle, const char *key, const char *value);` Store a value for a key. The library chooses the transfer protocol based on message size.
- `int kv_get(void *kv_handle, const char *key, char **value);` Retrieve a value associated with a key. Memory for the result is allocated by the library.
- `void kv_release(char *value);` Free memory obtained from `kv_get`.
- `int kv_close(void *kv_handle);` Disconnect from the server and release RDMA resources.

### RDMA Primitives

- `int RDMA_connect(char *servername, PingpongContext **save_ctx);` Establish an RDMA connection and initialize the context.
- `int RDMA_disconnect(PingpongContext *ctx);` Tear down an RDMA connection.
- `int send_packet(PingpongContext *ctx);` Transmit a control packet.
- `int receive_packet(PingpongContext *ctx);` Blocking receive of a control packet.
- `int receive_packet_async(PingpongContext *ctx);` Post a receive request without waiting.
- `int send_RDMA_read(PingpongContext *ctx, char *target, unsigned long size, void *remote_addr, uint32_t remote_key);` Read data directly from remote memory.
- `int send_RDMA_write(PingpongContext *ctx, const char *value, void *remote_addr, uint32_t remote_key);` Write data directly to remote memory.
- `int receive_fin(PingpongContext *ctx);` Wait for a completion marker after a rendezvous transfer.
- `int send_fin(PingpongContext *ctx);` Send a completion marker after finishing a rendezvous transfer.

### Server/Client Helpers

- `int run_server(void);` Start the server loop that services requests.
- `int run_client(char *servername);` Example client that exercises the key-value API.

## How the Key-Value Store Works

1. A client calls `kv_open` to establish an RDMA connection with the server.
2. For `kv_set` and `kv_get`, the protocol depends on the size of the key and value:
   - **Eager protocol** for messages smaller than 4 KB. The key and value are sent directly inside the request packet.
   - **Rendezvous protocol** for larger messages. The client and server first exchange buffer metadata; the client then performs an RDMA write (for `kv_set`) or RDMA read (for `kv_get`) using the provided remote address and key. A `send_fin`/`receive_fin` pair marks completion.
3. The server keeps each key in an in-memory database. Small values are stored inline, while large values reside in dynamically allocated buffers registered for RDMA access.

## Cleaning

Remove build artifacts with:

```sh
make clean
```

## License

This project is provided for educational purposes and may require an RDMA-capable environment to run properly.

