/*
 * rpc.c - Contains the API definitions and algorithms for the RPC system
 * Author: Tristan Thomas
 * Date: 1-5-2023
 */

#include "rpc.h"
#include "hash_table.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <endian.h>
#include <time.h>
#include <pthread.h>


/* redefining htobe64 and be64toh */
#if defined(htobe64) && !defined(htonll)
    #define htonll(x) htobe64(x)
#endif

#if defined(be64toh) && !defined(ntohll)
    #define ntohll(x) be64toh(x)
#endif

/* constants */
#define MAX_NAME_LEN 1000
#define NUM_ERROR_MESSAGES 12

/* flags */
#define FIND 'f'
#define CALL 'c'
#define FOUND 'y'
#define NOT_FOUND 'n'
#define CONSISTENT 'g'
#define INCONSISTENT 'b'

#define NONBLOCKING

struct rpc_server {
    int listenfd;
    int connectfd;
    hash_table_t *reg_procedures;
    hash_table_t *found_procedures;
};

struct rpc_client {
    int sockfd;
};

struct rpc_handle {
    uint32_t id;
};

/* used to store both handler and handler id in hash table */
struct handler_item {
    rpc_handler handler;
    uint32_t id;
};

/* error handling */
const char *error_messages[NUM_ERROR_MESSAGES] = {
        "Inconsistent data",
        "Memory allocation failed",
        "Invalid arguments",
        "Handler not found",
        "Socket creation failed",
        "Address info failure",
        "Connection lost",
        "Network failure",
        "Overlength",
        "Insertion failed",
        "Thread failed",
        "Invalid procedure name"
};

enum error_codes {
    INCONSISTENT_DATA = 0,
    MEMORY_ALL0CATION,
    INVALID_ARGUMENTS,
    HANDLER_NOT_FOUND,
    SOCKET_CREATION,
    ADDRESS_INFO,
    CONNECTION_LOST,
    NETWORK_FAIL,
    OVERLENGTH,
    INSERTION,
    THREAD,
    INVALID_NAME
};



static int send_size(size_t size, int sockfd);
static int recv_size(int sockfd, size_t *size);
static int send_string(char *message, int sockfd);
static int recv_string(size_t size, char *buffer, int sockfd);
static int send_data(int sockfd, rpc_data *data);
static int recv_data(int sockfd, rpc_data *buffer);
static int recv_int(int sockfd, int *data);
static int send_int(int sockfd, int data);
static int send_void(int sockfd, size_t size, void *data);
static int recv_void(int sockfd, size_t size, void *data);
static uint32_t hash_djb2(char* str);
static uint32_t hash_int(uint32_t* num);
static uint32_t generate_id();
int int_cmp(uint32_t *a, uint32_t *b);
static int send_flag(int sockfd, char data);
static int recv_flag(int sockfd, char *data);
static void *handle_connection(void *srv);
static void error_print(enum error_codes code);
static int is_valid_char(char c);
static int is_valid_name(char *name);



/**
 * Initialises data used for the server and creates listening socket
 *
 * @param port Port number
 * @return Rpc server data
 */
rpc_server *rpc_init_server(int port) {

    int enable = 1, s, listenfd;
    struct addrinfo hints, *res, *p;

    char port_str[6];

    struct rpc_server *server = malloc(sizeof(*server));

    if (!server) {
        error_print(MEMORY_ALL0CATION);
        return NULL;
    }

    // convert port to string
    sprintf(port_str, "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    s = getaddrinfo(NULL, port_str, &hints, &res);
    if (s != 0) {
        error_print(ADDRESS_INFO);
        return NULL;
    }

    // finding a valid IPv6 address
    for (p = res; p != NULL; p = p->ai_next) {
        // only creates socket if address is IPv6
        if (p->ai_family == AF_INET6 &&
                (listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) > 0) {
            // success
            break;
        }
    }

    if (listenfd < 0) {
        error_print(SOCKET_CREATION);
        return NULL;
    }


    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        error_print(SOCKET_CREATION);
        return NULL;
    }

    // bind
    if (bind(listenfd, res->ai_addr, res->ai_addrlen) < 0) {
        error_print(SOCKET_CREATION);
        return NULL;
    }

    // listen (blocking)
    if (listen(listenfd, 5) < 0) {
        error_print(SOCKET_CREATION);
        return NULL;
    }


    // assign to server
    server->listenfd = listenfd;

    freeaddrinfo(res);

    server->reg_procedures = create_empty_table();
    server->found_procedures = create_empty_table();


    return server;
}

/**
 * Initialises data used for the client
 *
 * @param addr Address of the server
 * @param port Port number
 * @return Rpc client data
 */
rpc_client *rpc_init_client(char *addr, int port) {

    int connectfd, s;
    struct addrinfo hints, *servinfo, *p;
    char port_str[6];
    struct rpc_client *client = malloc(sizeof(*client));
    if (!client) {
        error_print(MEMORY_ALL0CATION);
        return NULL;
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    // convert port to string
    sprintf(port_str, "%d", port);
    s = getaddrinfo(addr, port_str, &hints, &servinfo);
    if (s != 0) {
        error_print(ADDRESS_INFO);
        return NULL;
    }
    // connect to the server
    for (p = servinfo; p != NULL; p = p->ai_next) {
        connectfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (connectfd == -1) {
            continue;
        }
        if (connect(connectfd, p->ai_addr, p->ai_addrlen) != -1) {
            break;
        }
        close(connectfd);
    }

    if (p == NULL) {
        error_print(NETWORK_FAIL);
        return NULL;
    }
    // assign to client
    client->sockfd = connectfd;

    freeaddrinfo(servinfo);

    return client;
}

/**
 * Registers a procedure to the server by name
 *
 * @param srv Server struct
 * @param name Name procedure
 * @param handler Actual procedure
 * @return Procedure ID on success
 */
int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
    if (srv == NULL || name == NULL || handler == NULL) {
        error_print(INVALID_ARGUMENTS);
        return -1;
    } else if (!is_valid_name(name)) {
        error_print(INVALID_NAME);
        return -1;
    }
    struct handler_item *item = malloc(sizeof(*item));
    char *name_cpy = strdup(name);
    if (!item | !name_cpy) {
        error_print(MEMORY_ALL0CATION);
        return -1;
    }

    item->handler= handler;
    item->id = generate_id();
    // inserts procedure into hash table
    if (insert_data(srv->reg_procedures, name_cpy, (void *) item, (hash_func) hash_djb2, (compare_func) strcmp,
                    (free_func) free, NULL) == -1) {
        free(item);
        item = NULL;
        free(name_cpy);
        name_cpy = NULL;
        return -1;
    }
    return item->id;

}

/**
 * Accepts new connections from clients and completes requests
 *
 * @param srv Server data
 */
void rpc_serve_all(rpc_server *srv) {
    if (srv == NULL) {
        error_print(INVALID_ARGUMENTS);
        exit(EXIT_FAILURE);
    }
    // make connection
    struct sockaddr_in6 client_addr;
    socklen_t client_addr_size = sizeof(client_addr);

    while (1) {
        // accept connection from client (takes from listen queue)
        int connectfd = accept(srv->listenfd, (struct sockaddr *) &client_addr, &client_addr_size);
        if (connectfd < 0) {
            error_print(NETWORK_FAIL);
        }

        srv->connectfd = connectfd;
        // creates new thread for each connection
        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_connection, srv) != 0) {
            error_print(THREAD);
            close(connectfd);
        }
    }
}

/**
 * Handles rpc_find and call requests from a specific client
 *
 * @param arg Server data containing the connection socket
 * @return NULL on exit thread
 */
static void *handle_connection(void *arg) {
    // Retrieve the connection file descriptor from the argument
    rpc_server *srv = (rpc_server *) arg;
    int connectfd = srv->connectfd;

    char type;
    size_t size;
    char name[MAX_NAME_LEN + 1];

    while(1) {
        // type (either find or call)
        type = 0;
        if (recv_flag(connectfd, &type) == 0) {
            close(connectfd);
            pthread_exit(NULL);
        };

        switch(type) {
            // rpc_find request
            case FIND:
                // reads function name size
                if (recv_size(connectfd, &size) == 0
                    // reads function name
                    || recv_string(size, name, connectfd) == 0) {

                    close(connectfd);
                    pthread_exit(NULL);

                }
                // finds procedure given the name
                struct handler_item *item = (struct handler_item *) get_data(srv->reg_procedures, name, (hash_func) hash_djb2,
                        (compare_func) strcmp);

                if (item) {
                    if (send_flag(connectfd, FOUND) == -1
                        // send id to client
                        || send_int(connectfd, item->id) == -1) {

                        close(connectfd);
                        pthread_exit(NULL);

                    }
                    // insert procedure into found hash table
                    if (insert_data(srv->found_procedures, &item->id, (void *) item->handler, (hash_func) hash_int,
                                    (compare_func) int_cmp, NULL, NULL) == -1) {
                        error_print(INSERTION);
                    };

                } else {
                    error_print(HANDLER_NOT_FOUND);
                    if (send_flag(connectfd, NOT_FOUND) == -1) {
                        close(connectfd);
                        pthread_exit(NULL);
                    }
                }

                break;

            case CALL:
                // rpc_call request
                rpc_data *data = malloc(sizeof(*data));
                if (!data) {
                    error_print(MEMORY_ALL0CATION);
                    continue;
                }
                rpc_data *result;

                uint32_t id;
                // receive id from client
                if (recv_int(connectfd, (int *) &id) == 0
                    // receive data from client
                    || recv_data(connectfd, data) == 0) {

                    close(connectfd);
                    pthread_exit(NULL);

                }
                // get procedure using procedure id
                rpc_handler handler = (rpc_handler) get_data(srv->found_procedures, &id, (hash_func) hash_int,
                                                             (compare_func) int_cmp);

                result = handler(data);
                // checks for data consistency
                if (result == NULL) {
                    if (send_flag(connectfd, INCONSISTENT) == -1) {
                        close(connectfd);
                        pthread_exit(NULL);
                    }
                    error_print(INCONSISTENT_DATA);
                    rpc_data_free(data);
                    continue;

                } else if ((result->data2 && !result->data2_len) || (!result->data2 && result->data2_len)) {
                    if (send_flag(connectfd, INCONSISTENT) == -1) {
                        close(connectfd);
                        pthread_exit(NULL);
                    }
                    error_print(INCONSISTENT_DATA);
                    rpc_data_free(data);
                    rpc_data_free(result);
                    continue;
                }

                rpc_data_free(data);

                // notify client that data is consistent
                if (send_flag(connectfd, CONSISTENT) == -1
                    // send the consistent data
                    || send_data(connectfd, result) == -1) {

                    close(connectfd);
                    pthread_exit(NULL);

                }

                rpc_data_free(result);

                break;
        }


    }

}




/**
 * Finds a procedure on the server given a name
 *
 * @param cl Client data
 * @param name Query name
 * @return A handle containing the unique procedure ID upon success, NULL on failure
 */
rpc_handle *rpc_find(rpc_client *cl, char *name) {

    if (cl == NULL || name == NULL) {
        error_print(INVALID_ARGUMENTS);
        return NULL;
    } else if (!is_valid_name(name)) {
        error_print(INVALID_NAME);
        return NULL;
    }

    char found;
    rpc_handle *handle = NULL;

    // send type of request (find)
    if (send_flag(cl->sockfd, FIND) == -1
        // send function name size to server
        || send_size(strlen(name), cl->sockfd) == -1
        // send function name to server
        || send_string(name, cl->sockfd) == -1
        // receive whether procedure was found
        || recv_flag(cl->sockfd, &found) <= 0) {

        return NULL;

    }

    // if data is found
    if (found == FOUND) {
        handle = malloc(sizeof(*handle));
        if (!handle) {
            error_print(MEMORY_ALL0CATION);
            return NULL;
        }
        if (recv_int(cl->sockfd, (int *) &handle->id) <= 0) {
            free(handle);
            handle = NULL;
            return NULL;
        }
    }

    return handle;
}

/**
 * Calls a given procedure from the server given an ID
 *
 * @param cl Client data
 * @param h Handle containing ID
 * @param payload Data to be send to server
 * @return Output data from the procedure on success, NULL on failure
 */
rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {

    if (cl == NULL || h == NULL || payload == NULL) {
        error_print(INVALID_ARGUMENTS);
        return NULL;
    }

    // checks for consistent data
    if ((payload->data2 && !payload->data2_len) || (!payload->data2 && payload->data2_len)) {
        error_print(INCONSISTENT_DATA);
        return NULL;
    }
    rpc_data *result = malloc(sizeof(*result));
    if (!result) {
        error_print(MEMORY_ALL0CATION);
        return NULL;
    }
    char status;


    // send type of request
    if (send_flag(cl->sockfd, CALL) == -1
        // send the procedure id
        || send_int(cl->sockfd, h->id) == -1
        // send the payload
        || send_data(cl->sockfd, payload) == -1
        // send the consistency of the return data
        || recv_flag(cl->sockfd, &status) <= 0
        || status == INCONSISTENT
        // receive the return data if consistent
        || recv_data(cl->sockfd, result) <= 0) {

        return NULL;

    }

    return result;
}


/**
 * Sends data to a host
 *
 * @param sockfd Socket to be sent over
 * @param data Data to be sent
 * @return 0 on success
 */
static int send_data(int sockfd, rpc_data *data) {

    // send data_1 int
    if (send_int(sockfd, data->data1) == -1
        // send data_2_length
        || send_size(data->data2_len, sockfd) == -1) {

        return -1;

    }


    // send data_2
    if (data->data2_len > 0) {
        if (send_void(sockfd, data->data2_len, data->data2) == -1) {
            return -1;
        }
    }


    return 0;

}

/**
 * Sends the void data to a host
 *
 * @param sockfd Socket to be sent over
 * @param size Size of data to be sent
 * @param data Data to be sent
 * @return Number of byte read on success
 */
static int send_void(int sockfd, size_t size, void *data) {

    ssize_t n = send(sockfd, data, size, 0);
    if (n < 0) {
        error_print(NETWORK_FAIL);
        return -1;
    }

    return n;

}

/**
 * Sends a string over the network
 *
 * @param message String to be sent
 * @param sockfd Socket to be sent over
 * @return Number of bytes read on success
 */
static int send_string(char *message, int sockfd) {

    // Send message
    int n = send(sockfd, message, strlen(message), 0);
    if (n < 0) {
        error_print(NETWORK_FAIL);
        return -1;
    }

    return n;
}

/**
 * Sends size_t data to a host
 *
 * @param size Data to be sent
 * @param sockfd Socket to be sent over
 * @return Number of bytes sent on success
 */
static int send_size(size_t size, int sockfd) {

    // use 4 bytes since 100000 fits
    if (size > UINT32_MAX) {
        error_print(OVERLENGTH);
        return -1;
    }
    uint32_t size_n = htonl((uint32_t) size);

    ssize_t n = send(sockfd, &size_n, sizeof(size_n), 0);
    if (n < 0) {
        error_print(NETWORK_FAIL);
        return -1;
    }

    return n;
}

/**
 *
 * @param sockfd
 * @param data
 * @return
 */
static int send_int(int sockfd, int data) {

    // checks that the data is within the storage bounds
    if (data <= INT64_MIN || data >= INT64_MAX) {
        error_print(OVERLENGTH);
        return -1;
    }
    uint64_t data_n = (uint64_t) htonll(data);
    ssize_t n = send(sockfd, &data_n, sizeof(data_n), 0);
    if (n < 0) {
        error_print(NETWORK_FAIL);
        return -1;
    }

    return n;
}



/**
 * Receives a string from a host given its size
 *
 * @param size Size of string
 * @param buffer Buffer to store read string
 * @param sockfd Socket to be read over
 * @return Number of bytes read on success
 */
static int recv_string(size_t size, char *buffer, int sockfd) {

    size_t bytes_received = 0;
    while (1) {
        ssize_t n = recv(sockfd, buffer + bytes_received, size - bytes_received, 0);

        if (n < 0) {
            error_print(NETWORK_FAIL);
            return -1;
        } else if (n == 0) {
            error_print(CONNECTION_LOST);
            return n;
        } else {
            bytes_received += (size_t) n;
            if (bytes_received == size) {
                break;
            }
        }

    }
    buffer[bytes_received] = '\0';

    return bytes_received;

}


/**
 * Receives size_t data from a host
 *
 * @param sockfd Socket to be read over
 * @param size Buffer to be read into
 * @return Number of bytes read on success
 */
static int recv_size(int sockfd, size_t *size) {

    uint32_t message_size = 0;

    size_t bytes_received = 0;
    size_t s_size = sizeof(uint32_t);
    uint32_t host_size;
    while (1) {
        ssize_t n = recv(sockfd, &message_size + bytes_received, s_size - bytes_received, 0);

        if (n < 0) {
            error_print(NETWORK_FAIL);
            return -1;
        } else if (n == 0) {
            error_print(CONNECTION_LOST);
            return n;
        } else {
            bytes_received += (size_t) n;
            if (bytes_received == s_size) {
                break;
            }
        }

    }

    // check if the valid received 32-bit data will fit within the size_t size of this host
    host_size = ntohl(message_size);
    if (host_size > SIZE_MAX) {
        error_print(OVERLENGTH);
        return -1;
    }

    *size = (size_t) host_size;

    return bytes_received;

}

/**
 * Receives data from a host
 *
 * @param sockfd Socket to be read over
 * @param buffer RPC_data buffer to be read into
 * @return
 */
static int recv_data(int sockfd, rpc_data *buffer) {

    int s;
    // receiving data_1 int
    s = recv_int(sockfd, &buffer->data1);
    if (s <= 0) {
        return s;
    }

    // receiving data_2 length
    s = recv_size(sockfd, &buffer->data2_len);
    if (s <= 0) {
        return s;
    }

    // receiving data_2
    if (buffer->data2_len > 0) {
        buffer->data2 = malloc(buffer->data2_len);
        if (!buffer->data2) {
            error_print(MEMORY_ALL0CATION);
            return -1;
        }

        s = recv_void(sockfd, buffer->data2_len, buffer->data2);
        if (s <= 0) {
            return s;
        }
    } else {
        buffer->data2 = NULL;
    }



    return 1;
}


/**
 * Receives the void pointed to data from rpc_data from a host
 *
 * @param sockfd Socket to be read over
 * @param size Size of this void pointed to data
 * @param data Buffer to read data into
 * @return Number of bytes read on success
 */
static int recv_void(int sockfd, size_t size, void *data) {

    size_t bytes_received = 0;
    while(1) {
        ssize_t n = recv(sockfd, data + bytes_received, size - bytes_received, 0);
        if (n < 0) {
            error_print(NETWORK_FAIL);
            return -1;
        } else if (n == 0) {
            error_print(CONNECTION_LOST);
            return n;
        } else {
            bytes_received += (size_t) n;
            if (bytes_received == size) {
                break;
            }
        }
    }


    return bytes_received;
}

/**
 * Receives an integer from a host
 *
 * @param sockfd Socket to be read over
 * @param num Buffer to store read int
 * @return Number of bytes read on success
 */
static int recv_int(int sockfd, int *num) {

    size_t bytes_received = 0;
    size_t int64_size = sizeof(uint64_t);
    uint64_t data;
    uint64_t host_data;
    while (1) {
        ssize_t n = recv(sockfd, &data + bytes_received, int64_size - bytes_received, 0);

        if (n < 0) {
            error_print(NETWORK_FAIL);
            return -1;
        } else if (n == 0) {
            error_print(CONNECTION_LOST);
            return n;
        } else {
            bytes_received += (size_t) n;
            if (bytes_received == int64_size) {
                break;
            }
        }

    }
    // check if the valid received 64-bit data will fit within the int size of this host
    host_data = ntohll(data);
    if ((int64_t) host_data > INT_MAX || (int64_t) host_data < INT_MIN) {
        error_print(OVERLENGTH);
        return -1;
    }

    *num = (int) host_data;

    return bytes_received;
}

/**
 * Receives a character flag from a host
 *
 * @param sockfd Socket to be read over
 * @param data Buffer to read character
 * @return Number of bytes read on success
 */
static int recv_flag(int sockfd, char *data) {

    ssize_t n = recv(sockfd, data, sizeof(*data), 0);

    if (n < 0) {
        error_print(NETWORK_FAIL);
        return -1;
    } else if (n == 0) {
        return n;
    }

    return n;
}

/**
 * Sends flag character to a host
 *
 * @param sockfd Socket to be sent over
 * @param data Flag to send
 * @return Number of bytes sent on success
 */
static int send_flag(int sockfd, char data) {

    ssize_t n = send(sockfd, &data, sizeof(data), 0);
    if (n < 0) {
        error_print(NETWORK_FAIL);
        return -1;
    }

    return n;
}


/**
 * Hash function for strings written by Daniel J. Bernstein
 * Was taken from: https://theartincode.stanis.me/008-djb2/
 *
 * @param str String to be hashed
 * @return Hash value
 */
static uint32_t hash_djb2(char* str) {

    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

/**
 * Simply returns the inputted value
 *
 * @param num Number to be used as index
 * @return Inputted number
 */
static uint32_t hash_int(uint32_t* num) {

    return *num;
}

/**
 * Generates a unique ID that is used as procedure ID's. Uses time and a counter to ensure uniqueness
 *
 * @return Generated ID
 */
static uint32_t generate_id() {

    static uint32_t counter = 0;
    time_t curr_time = time(NULL);
    return (uint32_t) curr_time + counter++;
}

/**
 * Integer comparison function that can be used for the hash table
 *
 * @param a First integer
 * @param b Second integer
 * @return Value depending on comparison
 */
int int_cmp(uint32_t *a, uint32_t *b) {

    if (*a < *b) {
        return -1;
    } else if (*a > *b) {
        return 1;
    } else {
        return 0;
    }
}

/**
 * Checks if a given character is valid for a procedure name
 *
 * @param c Character to be checked
 * @return Result of check
 */
static int is_valid_char(char c) {

    return (c >= 32 && c <= 126);
}

/**
 * Checks if a given name has all valid characters
 *
 * @param name Name to be checked
 * @return Result of check
 */
static int is_valid_name(char *name) {

    for (int i = 0; name[i] != '\0'; i++) {
        // Check if the character is valid
        if (!is_valid_char(name[i])) {
            return 0;
        }
    }
    return 1;
}

/**
 * Prints an error message given an error code
 *
 * @param code Error code
 */
static void error_print(enum error_codes code) {

    fprintf(stderr, "Error: %s\n", error_messages[code]);
}

/**
 * Closes client socket and data
 *
 * @param cl Client to be closed
 */
void rpc_close_client(rpc_client *cl) {

    if (cl) {
        close(cl->sockfd);
        free(cl);
        cl = NULL;
    }

}


/**
 * Frees an RPC data struct
 *
 * @param data Data to be freed
 */
void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
    data = NULL;
}



