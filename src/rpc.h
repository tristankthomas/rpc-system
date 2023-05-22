/*
 * rpc.h - Contains the API for the RPC system
 * Author: Tristan Thomas
 * Date: 1-5-2023
 */

#ifndef RPC_H
#define RPC_H

#include <stddef.h>

/* Server state */
typedef struct rpc_server rpc_server;
/* Client state */
typedef struct rpc_client rpc_client;

/* The payload for requests/responses */
typedef struct {
    int data1;
    size_t data2_len;
    void *data2;
} rpc_data;

/* Handle for remote function */
typedef struct rpc_handle rpc_handle;

/* Handler for remote functions, which takes rpc_data* as input and produces
 * rpc_data* as output */
typedef rpc_data *(*rpc_handler)(rpc_data *);

/* ---------------- */
/* Server functions */
/* ---------------- */

/**
 * Initialises data used for the server and creates listening socket
 *
 * @param port Port number
 * @return Rpc server data
 */
rpc_server *rpc_init_server(int port);

/**
 * Registers a procedure to the server by name
 *
 * @param srv Server struct
 * @param name Name procedure
 * @param handler Actual procedure
 * @return Procedure ID on success
 */
int rpc_register(rpc_server *srv, char *name, rpc_handler handler);

/**
 * Accepts new connections from clients and completes requests
 *
 * @param srv Server data
 */
void rpc_serve_all(rpc_server *srv);

/* ---------------- */
/* Client functions */
/* ---------------- */

/**
 * Initialises data used for the client
 *
 * @param addr Address of the server
 * @param port Port number
 * @return Rpc client data
 */
rpc_client *rpc_init_client(char *addr, int port);

/**
 * Finds a procedure on the server given a name
 *
 * @param cl Client data
 * @param name Query name
 * @return A handle containing the unique procedure ID upon success, NULL on failure
 */
rpc_handle *rpc_find(rpc_client *cl, char *name);

/**
 * Calls a given procedure from the server given an ID
 *
 * @param cl Client data
 * @param h Handle containing ID
 * @param payload Data to be send to server
 * @return Output data from the procedure on success, NULL on failure
 */
rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload);

/**
 * Closes client socket and data
 *
 * @param cl Client to be closed
 */
void rpc_close_client(rpc_client *cl);

/* ---------------- */
/* Shared functions */
/* ---------------- */

/**
 * Frees an RPC data struct
 *
 * @param data Data to be freed
 */
void rpc_data_free(rpc_data *data);

#endif
