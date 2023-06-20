# Multi-threaded Remote Procedure Call System
This project was created for a computer systems and networks subject and involved designing and implementing an API for a remote procedure call (RPC) system to enable a client to seamlessly perform function calls on a remote server. Servers are to register and store certain functions and then handle many simultaneous requests to these procedures, using multithreading. A procedure takes a single data struct as an argument and returns a modified data struct which can be sent back and forth between server and client. This system runs over TCP and IPv6 and is designed to be system independent. It can work on systems with different byte ordering and size_t/int sizes by sending packets over the network in a standardised form.
## API Methods
The API contains a range of methods that can be accessed by clients and servers through the header file. These include:
### Client
1. `rpc_init_client` - This method initiates the client socket and connects it to an RPC server based on the port number inputted by the client. This connected socket is then stored in an `rpc_client` struct which is passed into all other client methods.
2. `rpc_find` - This method is used to check if a procedure is available on the server by the name inputted and if found, stores a unique ID for this procedure in another struct, `rpc_handle`, which is used from then on to call this procedure.
3. `rpc_call` - This method takes in a procedure handle returned from `rpc_find` as well as an `rpc_data` struct and calls this handle on the server, returning another data struct that resulted from the called procedure. An `rpc_data` struct contains two pieces of data: `data1` which is simply an int and `data2` which can be of any type (stream of bytes).
4. `rpc_close_client` - This method simply closes the connection socket between client and server, called when the client has finished with the remote procedures.
### Server
1. `rpc_init_server` - The purpose of this method is to create a socket that can listen for incoming client connections and place them in a queue. This socket, along with empty hash-tables (for procedures), are stored in a struct called `rpc_server` which is once again passed into all other methods.
2. `rpc_register` - This method is used to register a particular function (that is implemented in the server) by name, and storing this in a hashtable that can easily be accessed using the name as a key.
3. `rpc_serve_all` - This is the main method that is used to accept connections from multiple clients by passing new clients to new worker threads, and then continues to block until a new client is available. A worker thread handles both 'find' and 'call' requests and continues working until the connection is interrupted.
## Usage
The provided Makefile builds the RPC API into a static library which is linked to an example server/client. Any custom server/client can be linked with the system by making minor adjustments to this Makefile.

Once compiled, the server can first be executed in the foreground by:
```
./rpc-server -p <port> &
```
Next clients can be ran by:
```
./rpc-client -i <ip-address> -p <port>
```
Where:
- `ip-address` is the IPv6 address of the server.
- `port` is the TCP port number of the server.