#include "rpc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <endian.h>
#include <time.h>
#include <pthread.h>
#include "hash_table.h"

// use to change depending on system
#if defined(htobe64) && !defined(htonll)
    #define htonll(x) htobe64(x)
#endif

#if defined(be64toh) && !defined(ntohll)
    #define ntohll(x) be64toh(x)
#endif


#define FIND 'f'
#define CALL 'c'
#define DEFAULT 'd'
#define FOUND 'y'
#define NOT_FOUND 'n'

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

struct handler_item {
    rpc_handler handler;
    uint32_t id;
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
int intcmp(uint32_t *a, uint32_t *b);
static int send_type(int sockfd, char data);
static int recv_type(int sockfd, char *data);
void *handle_connection(void *srv);




rpc_server *rpc_init_server(int port) {

    int enable = 1, s, listenfd;
    struct addrinfo hints, *res, *p;

    char port_str[6];

    struct rpc_server *server = malloc(sizeof(*server));
    assert(server);

    // convert port to string
    sprintf(port_str, "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    s = getaddrinfo(NULL, port_str, &hints, &res);
    if (s != 0) {
        fprintf(stderr, "Address info failure\n");
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
        fprintf(stderr, "Failed to create socket\n");
        return NULL;
    }


    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        fprintf(stderr, "Failed to set options\n");
        return NULL;
    }

    // bind
    if (bind(listenfd, res->ai_addr, res->ai_addrlen) < 0) {
        fprintf(stderr, "Failed to bind\n");
        return NULL;
    }

    // listen (blocking)
    if (listen(listenfd, 5) < 0) {
        fprintf(stderr, "Failed to listen\n");
        return NULL;
    }


    // assign to server
    server->listenfd = listenfd;

    freeaddrinfo(res);

    server->reg_procedures = create_empty_table();
    server->found_procedures = create_empty_table();


    return server;
}


rpc_client *rpc_init_client(char *addr, int port) {
    int connectfd, s;
    struct addrinfo hints, *servinfo, *p;
    char port_str[6];
    struct rpc_client *client = malloc(sizeof(*client));

    assert(client);


    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    // convert port to string
    sprintf(port_str, "%d", port);
    s = getaddrinfo(addr, port_str, &hints, &servinfo);
    if (s != 0) {
        fprintf(stderr, "Address info failure\n");
        return NULL;
    }
    // connect to the server
    for (p = servinfo; p != NULL; p = p->ai_next) {
        connectfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (connectfd == -1) {
            continue;
        }
        if (connect(connectfd, p->ai_addr, p->ai_addrlen) != -1) {
//            printf("connection successfully made\n");
            break;
        }
        close(connectfd);
    }

    if (p == NULL) {
        fprintf(stderr, "Failed to connect to server\n");
        return NULL;
    }
    // assign to client
    client->sockfd = connectfd;

    freeaddrinfo(servinfo);


    return client;
}

int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
    if (srv == NULL || name == NULL || handler == NULL) {
        fprintf(stderr, "Invalid arguments\n");
        return -1;
    }
    struct handler_item *item = malloc(sizeof(*item));
    char *name_cpy = strdup(name);
    // change these asserts to return -1
    assert(name_cpy);
    assert(item);
    item->handler= handler;
    item->id = generate_id();
    // add error handling here
    if (insert_data(srv->reg_procedures, name_cpy, (void *) item, (hash_func) hash_djb2, (compare_func) strcmp) == -1) {
        return -1;
    }
//    printf("%s function registered\n", name_cpy);
    return item->id;

}

void rpc_serve_all(rpc_server *srv) {
    if (srv == NULL) {
        fprintf(stderr, "Invalid arguments\n");
        exit(EXIT_FAILURE);
    }
    // make connection
    struct sockaddr_in6 client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    char ip[INET6_ADDRSTRLEN];
    int port;

    int count = 0;
    while (1) {
        // accept connection from client (takes from listen queue)
//        printf("accepting another connection\n");
        int connectfd = accept(srv->listenfd, (struct sockaddr *) &client_addr, &client_addr_size);
        if (connectfd < 0) {
            fprintf(stderr, "Connection failed\n");
        }
//        printf("%d\n", count++);

        // show connection
        // Print ipv4 peer information
        getpeername(connectfd, (struct sockaddr *) &client_addr, &client_addr_size);
        inet_ntop(client_addr.sin6_family, &client_addr.sin6_addr, ip, INET6_ADDRSTRLEN);
        // client port
        port = ntohs(client_addr.sin6_port);
//        printf("new connection from %s:%d on socket %d\n", ip, port, connectfd);
        srv->connectfd = connectfd;
        // Create a new thread for each accepted connection
        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_connection, srv) != 0) {
            fprintf(stderr, "Thread failed\n");
            close(connectfd);
        }
    }
}

void *handle_connection(void *arg) {
    // Retrieve the connection file descriptor from the argument
    rpc_server *srv = (rpc_server *) arg;
    int connectfd = srv->connectfd;

    char type;
    size_t size;
    char name[255];

    while(1) {
        // type (either find or call)
        type = DEFAULT;
        if (recv_type(connectfd, &type) == 0) {
            close(connectfd);
            pthread_exit(NULL);
        };

        switch(type) {
            case FIND:
                // reads function name size
                if (recv_size(connectfd, &size) == 0) {
                    close(connectfd);
                    pthread_exit(NULL);
                };
//                printf("function name size is %zu\n", size);

                // reads function name
                if (recv_string(size, name, connectfd) == 0) {
                    close(connectfd);
                    pthread_exit(NULL);
                };
//                printf("Here is the function name: %s\n", name);
                struct handler_item *item = (struct handler_item *) get_data(srv->reg_procedures, name, (hash_func) hash_djb2, (compare_func) strcmp);

//                printf("function found: sending id to client\n");

                if (item) {
                    if (send_type(connectfd, FOUND) == -1) {
                        close(connectfd);
                        pthread_exit(NULL);
                    }
                    // send id to client
                    if (send_int(connectfd, item->id) == -1) {
                        close(connectfd);
                        pthread_exit(NULL);
                    }

                    if (insert_data(srv->found_procedures, &item->id, (void *) item->handler, (hash_func) hash_int, (compare_func) intcmp) == -1) {
                        fprintf(stderr, "Insertion error");
                    };

                } else {
                    fprintf(stderr, "Handler not found\n");
                    if (send_type(connectfd, NOT_FOUND) == -1) {
                        close(connectfd);
                        pthread_exit(NULL);
                    }
                }

                break;

            case CALL:
                rpc_data *data = malloc(sizeof(*data));
                assert(data);
                rpc_data *result;

                uint32_t id = 0;
                if (recv_int(connectfd, (int *) &id) == 0) {
//                    printf("closing connection\n");
                    close(connectfd);
                    pthread_exit(NULL);
                }
//                printf("received id %d\n", id);

                if (recv_data(connectfd, data) == 0) {
//                    printf("closing connection\n");
                    close(connectfd);
                    pthread_exit(NULL);
                }

                result = ((rpc_handler) get_data(srv->found_procedures, &id, (hash_func) hash_int, (compare_func) intcmp))(data);
                if ((result->data2 && !result->data2_len) || (!result->data2 && result->data2_len)) {
                    fprintf(stderr, "Inconsistent data\n");
                    close(connectfd);
                    pthread_exit(NULL);
                }
                rpc_data_free(data);
//                printf("result is %d\n", result->data1);

                if (send_data(connectfd, result) == -1) {
                    close(connectfd);
                    pthread_exit(NULL);
                }

                rpc_data_free(result);

                break;
            case DEFAULT:
                continue;
        }


    }

}






rpc_handle *rpc_find(rpc_client *cl, char *name) {
    //printf("CLIENT: finding function %s \n", name);
    if (cl == NULL || name == NULL) {
        fprintf(stderr, "Invalid arguments\n");
        return NULL;
    }
    char found;
    rpc_handle *handle = NULL;
    if (send_type(cl->sockfd, FIND) == -1) {
        return NULL;
    }

    // send function name size to server
    if (send_size(strlen(name), cl->sockfd) == -1) {
        return NULL;
    }

    // send function name to server
    if (send_string(name, cl->sockfd) == -1) {
        return NULL;
    }

    if (recv_type(cl->sockfd, &found) <= 0) {
        return NULL;
    }

    if (found == FOUND) {
        handle = malloc(sizeof(*handle));
        assert(handle);
        if (recv_int(cl->sockfd, (int *) &handle->id) <= 0) {
            free(handle);
            return NULL;
        }
//        printf("received function id: %d\n", handle->id);
    }

    return handle;
}


rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    if (cl == NULL || h == NULL || payload == NULL) {
        fprintf(stderr, "Invalid arguments\n");
        return NULL;
    }

    if ((payload->data2 && !payload->data2_len) || (!payload->data2 && payload->data2_len)) {
        fprintf(stderr, "Inconsistent data\n");
        return NULL;
    }
    rpc_data *result = malloc(sizeof(*result));
    assert(result);

    if (send_type(cl->sockfd, CALL) == -1) {
        return NULL;
    }

    // send function id to server
//    printf("sending id\n");
    if (send_int(cl->sockfd, h->id) == -1) {
        return NULL;
    }

    // send rpc_data to server
//    printf("sending data\n");
    if (send_data(cl->sockfd, payload) == -1) {
        return NULL;
    }

    if (recv_data(cl->sockfd, result) <= 0) {
        return NULL;
    }



    return result;
}






// sending functions

static int send_data(int sockfd, rpc_data *data) {

    // send data_1 int

//    printf("sending data_1: %d\n", data_1);
    if (send_int(sockfd, data->data1) == -1) {
        return -1;
    }


    // send data_2_length
//    printf("sending data_2_length: %zu\n", size);
    if (send_size(data->data2_len, sockfd) == -1) {
        return -1;
    }

    // send data_2
    if (data->data2_len > 0) {
//        printf("sending data_2\n");
        if (send_void(sockfd, data->data2_len, data->data2) == -1) {
            return -1;
        }
    }


    return 0;

}

static int send_void(int sockfd, size_t size, void *data) {

    ssize_t n = send(sockfd, data, size, 0);
    if (n < 0) {
        fprintf(stderr, "Network error sending\n");
        return -1;
    }

    return n;

}

static int send_string(char *message, int sockfd) {

    // Send message
    int n = send(sockfd, message, strlen(message), 0);
    if (n < 0) {
        fprintf(stderr, "Network error sending\n");
        return -1;
    }

    return n;
}

static int send_size(size_t size, int sockfd) {

    // use 4 bytes since 100000 fits
    if (size > UINT32_MAX) {
        fprintf(stderr, "Overlength error\n");
        return -1;
    }
    uint32_t size_n = htonl((uint32_t) size);

    ssize_t n = send(sockfd, &size_n, sizeof(size_n), 0);
    if (n < 0) {
        fprintf(stderr, "Network error sending\n");
        return -1;
    }

    return n;
}

static int send_int(int sockfd, int data) {
    if (data <= INT64_MIN || data >= INT64_MAX) {
        fprintf(stderr, "Overlength error\n");
        return -1;
    }
    uint64_t data_n = (uint64_t) htonll(data);
    ssize_t n = send(sockfd, &data_n, sizeof(data_n), 0);
    if (n < 0) {
        fprintf(stderr, "Network error sending\n");
        return -1;
    }

    return n;
}




// receiving functios
static int recv_string(size_t size, char *buffer, int sockfd) {

    size_t bytes_received = 0;
//    printf("receiving message\n");
    while (1) {
        ssize_t n = recv(sockfd, buffer + bytes_received, size - bytes_received, 0);

        if (n < 0) {
            fprintf(stderr, "Network error receiving\n");
            return -1;
        } else if (n == 0) {
            fprintf(stderr, "Connection lost\n");
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




static int recv_size(int sockfd, size_t *size) {


    uint32_t message_size = 0;

    size_t bytes_received = 0;
//    printf("receiving message size\n");
    size_t s_size = sizeof(uint32_t);
    while (1) {
        ssize_t n = recv(sockfd, &message_size + bytes_received, s_size - bytes_received, 0);

        if (n < 0) {
            fprintf(stderr, "Network error receiving\n");
            return -1;
        } else if (n == 0) {
            fprintf(stderr, "Connection lost\n");
            return n;
        } else {
            bytes_received += (size_t) n;
            if (bytes_received == s_size) {
                break;
            }
        }

    }

    message_size = ntohl(message_size);

    *size = (size_t) message_size;

    return bytes_received;

}

static int recv_data(int sockfd, rpc_data *buffer) {
    int s;
    // receiving data_1 int
    s = recv_int(sockfd, &buffer->data1);
    if (s <= 0) {
        return s;
    }
//    printf("received data1: %d\n", buffer->data1);

    // receiving data_2 length
    s = recv_size(sockfd, &buffer->data2_len);
    if (s <= 0) {
        return s;
    }
//    printf("received data2_len: %zu\n", buffer->data2_len);

    // receiving data_2
    if (buffer->data2_len > 0) {
        buffer->data2 = malloc(buffer->data2_len);
        assert(buffer->data2);

        s = recv_void(sockfd, buffer->data2_len, buffer->data2);
        if (s <= 0) {
            return s;
        }
//        if (buffer->data2 != NULL) printf("data is %d\n",  *((int *) buffer->data2));
    } else {
        buffer->data2 = NULL;
//        printf("no bytes read\n");
    }



    return 1;
}

static int recv_void(int sockfd, size_t size, void *data) {
    size_t bytes_received = 0;
    while(1) {
        ssize_t n = recv(sockfd, data + bytes_received, size - bytes_received, 0);
        if (n < 0) {
            fprintf(stderr, "Network error\n");
            return -1;
        } else if (n == 0) {
            fprintf(stderr, "Connection lost\n");
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

static int recv_int(int sockfd, int *num) {
    size_t bytes_received = 0;
    size_t int64_size = sizeof(uint64_t);
    uint64_t data;
    while (1) {
        ssize_t n = recv(sockfd, &data + bytes_received, int64_size - bytes_received, 0);

        if (n < 0) {
            fprintf(stderr, "Network error\n");
            return -1;
        } else if (n == 0) {
            fprintf(stderr, "Connection lost\n");
            return n;
        } else {
            bytes_received += (size_t) n;
            if (bytes_received == int64_size) {
                break;
            }
        }

    }
    // need to check if the received 64-bit data (we know it is valid!) will fit within the int size of this host
    // (could check by going : if ((INT64_t) data > INT_MAX || (INT64_t) data < INT_MIN) return overlength error)

    *num = (int) ntohll(data);

    return bytes_received;
}

static int recv_type(int sockfd, char *data) {

    ssize_t n = recv(sockfd, data, sizeof(*data), 0);

    if (n < 0) {
        fprintf(stderr, "Network error\n");
        return -1;
    } else if (n == 0) {
        return n;
    }

    return n;
}

static int send_type(int sockfd, char data) {

    ssize_t n = send(sockfd, &data, sizeof(data), 0);
    if (n < 0) {
        fprintf(stderr, "Network error\n");
        return -1;
    }

    return n;
}


// hash functions
static uint32_t hash_djb2(char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static uint32_t hash_int(uint32_t* num) {
    return *num;
}

static uint32_t generate_id() {
    static uint32_t counter = 0;
    time_t curr_time = time(NULL);
    return (uint32_t) curr_time + counter++;
}

int intcmp(uint32_t *a, uint32_t *b) {
    if (*a < *b) {
        return -1;
    } else if (*a > *b) {
        return 1;
    } else {
        return 0;
    }
}


void rpc_close_client(rpc_client *cl) {
    if (cl) {
        close(cl->sockfd);
        free(cl);
        cl = NULL;
    }

}

void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}

