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
#include "hash_table.h"

// use to change depending on system
#if defined(htobe64) && !defined(htonll)
    #define htonll(x) htobe64(x)
#endif

#if defined(be64toh) && !defined(ntohll)
    #define ntohll(x) be64toh(x)
#endif

#if defined(__SIZEOF_SIZE_T__)
    #if __SIZEOF_SIZE_T__ == 2
        typedef uint16_t uintsize_t;
        #define htonsize(x) htons(x)
        #define ntohsize(x) ntohs(x)
    #elif __SIZEOF_SIZE_T__ == 4
        typedef uint32_t uintsize_t;
        #define htonsize(x) htonl(x)
        #define ntohsize(x) ntohl(x)
    #elif __SIZEOF_SIZE_T__ == 8
        typedef uint64_t uintsize_t;
        #define htonsize(x) htonll(x)
        #define ntohsize(x) ntohll(x)
    #else
        #error "Unsupported size for size_t"
    #endif
#else
    #error "Cannot determine size of size_t"
#endif


#define FIND 'f'
#define CALL 'c'
#define FOUND 'y'
#define NOT_FOUND 'n'

struct rpc_server {
    int sockfd;
    int listenfd;
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




static size_t send_size(size_t size, int sockfd);
static size_t recv_size(int sockfd);
static size_t send_string(char *message, int sockfd);
static size_t recv_string(size_t size, char *buffer, int sockfd);
static size_t send_data(int sockfd, rpc_data *data);
static size_t recv_data(int sockfd, rpc_data *buffer);
static size_t recv_int(int sockfd, int *data);
static size_t send_int(int sockfd, int data);
static size_t send_void(int sockfd, size_t size, void *data);
static size_t recv_void(int sockfd, size_t size, void *data);
static uint32_t hash_djb2(char* str);
static uint32_t hash_int(uint32_t* num);
static uint32_t generate_id();
int intcmp(uint32_t *a, uint32_t *b);
static size_t send_type(int sockfd, char data);
static size_t recv_type(int sockfd, char *data);




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
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
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
        perror("socket");
        return NULL;
    }


    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt");
        return NULL;
    }

    // bind
    if (bind(listenfd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind");
        return NULL;
    }

    // listen (blocking)
    if (listen(listenfd, 5) < 0) {
        perror("listen");
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
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return NULL;
    }
    // connect to the server
    for (p = servinfo; p != NULL; p = p->ai_next) {
        connectfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (connectfd == -1) {
            continue;
        }
        if (connect(connectfd, p->ai_addr, p->ai_addrlen) != -1) {
            printf("connection successfully made\n");
            break;
        }
        close(connectfd);
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return NULL;
    }
    // assign to client
    client->sockfd = connectfd;

    freeaddrinfo(servinfo);


    return client;
}

int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
    if (srv == NULL || name == NULL || handler == NULL) {
        return -1;
    }
    struct handler_item *item = malloc(sizeof(*item));
    char *name_cpy = strdup(name);
    assert(name_cpy);
    assert(item);
    item->handler= handler;
    item->id = generate_id();
    insert_data(srv->reg_procedures, name_cpy, (void *) item, (hash_func) hash_djb2, (compare_func) strcmp);
    printf("%s function registered\n", name_cpy);
    return item->id;

}

void rpc_serve_all(rpc_server *srv) {
    char name[255];

    // make connection
    struct sockaddr_in6 client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    char ip[INET6_ADDRSTRLEN];
    int port;
    // accept connection from client (takes from listen queue)
    int connectfd = accept(srv->listenfd, (struct sockaddr *) &client_addr, &client_addr_size);
    if (connectfd < 0) {
        perror("accept");
        return;
    }
    srv->sockfd = connectfd;

    // Print ipv4 peer information (can be removed)
    getpeername(connectfd, (struct sockaddr *) &client_addr, &client_addr_size);
    inet_ntop(client_addr.sin6_family, &client_addr.sin6_addr, ip, INET6_ADDRSTRLEN);
    // client port
    port = ntohs(client_addr.sin6_port);
    printf("new connection from %s:%d on socket %d\n", ip, port, connectfd);

    char type;
    int size;

    while(1) {
        // type (either find or call)
        recv_type(srv->sockfd, &type);

        switch(type) {
            case FIND:
                // reads function name size
                size = recv_size(srv->sockfd);
                printf("function name size is %d\n", size);

                // reads function name
                recv_string(size, name, srv->sockfd);
                printf("Here is the function name: %s\n", name);
                struct handler_item *item = (struct handler_item *) get_data(srv->reg_procedures, name, (hash_func) hash_djb2, (compare_func) strcmp);
                printf("function found: sending id to client\n");

                if (item) {
                    send_type(srv->sockfd, FOUND);
                    // send id to client
                    send_int(srv->sockfd, item->id);

                } else {
                    send_type(srv->sockfd, NOT_FOUND);
                    continue;
                }



                insert_data(srv->found_procedures, &item->id, (void *) item->handler, (hash_func) hash_int, (compare_func) intcmp);

                break;
            case CALL:
                rpc_data *data = malloc(sizeof(*data));
                assert(data);
                rpc_data *result;

                uint32_t id = 0;
                recv_int(srv->sockfd, (int *) &id);
                printf("received id %d\n", id);

                recv_data(srv->sockfd, data);

                result = ((rpc_handler) get_data(srv->found_procedures, &id, (hash_func) hash_int, (compare_func) intcmp))(data);

                rpc_data_free(data);
                printf("result is %d\n", result->data1);

                send_data(srv->sockfd, result);

                rpc_data_free(result);

                break;
        }


    }


}




rpc_handle *rpc_find(rpc_client *cl, char *name) {
    //printf("CLIENT: finding function %s \n", name);
    if (cl == NULL || name == NULL) {
        return NULL;
    }
    char found;
    rpc_handle *handle = NULL;
    send_type(cl->sockfd, FIND);

    // send function name size to server
    send_size(strlen(name), cl->sockfd);

    // send function name to server
    send_string(name, cl->sockfd);
    recv_type(cl->sockfd, &found);

    if (found == FOUND) {
        handle = malloc(sizeof(*handle));
        assert(handle);
        recv_int(cl->sockfd, (int *) &handle->id);
        printf("received function id: %d\n", handle->id);
    }

    return handle;
}


rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    if (cl == NULL || h == NULL || payload == NULL) {
        return NULL;
    }
    rpc_data *result = malloc(sizeof(*result));
    assert(result);

    send_type(cl->sockfd, CALL);

    // send function id to server
    printf("sending id\n");
    send_int(cl->sockfd, h->id);

    // send rpc_data to server
    printf("sending data\n");
    send_data(cl->sockfd, payload);

    recv_data(cl->sockfd, result);


    return result;
}






// sending functions

static size_t send_data(int sockfd, rpc_data *data) {
    int data_1 = data->data1;
    size_t size = data->data2_len;
    void *data_2 = data->data2;

    // send data_1 int

    printf("sending data_1: %d\n", data_1);
    send_int(sockfd, data_1);


    // send data_2_length
    printf("sending data_2_length: %zu\n", size);
    send_size(size, sockfd);

    // send data_2
    if (size > 0) {
        printf("sending data_2\n");
        send_void(sockfd, size, data_2);
    }


    return 0;

}

static size_t send_void(int sockfd, size_t size, void *data) {

    int n = send(sockfd, data, size, 0);
    if (n < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    return n;

}

static size_t send_string(char *message, int sockfd) {

    // Send message
    int n = send(sockfd, message, strlen(message), 0);
    if (n < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    return n;
}

static size_t send_size(size_t size, int sockfd) {


    uintsize_t size_n = htonsize((uintsize_t) size);

    int n = send(sockfd, &size_n, sizeof(size_n), 0);
    if (n < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    return n;
}

static size_t send_int(int sockfd, int data) {
    int data_n = htonl(data);
    int n = send(sockfd, &data_n, sizeof(data_n), 0);
    if (n < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    return n;
}




// receiving functios
static size_t recv_string(size_t size, char *buffer, int sockfd) {

    int bytes_received = 0;
    printf("receiving message\n");
    while (1) {
        int n = recv(sockfd, buffer + bytes_received, size - bytes_received, 0);

        if (n < 0) {
            perror("recv");
            exit(EXIT_FAILURE);
        } else if (n == 0) {
            return n;
        } else {
            bytes_received += n;
            if (bytes_received == size) {
                break;
            }
        }

    }
    buffer[bytes_received] = '\0';

    return bytes_received;

}




static size_t recv_size(int sockfd) {


    uintsize_t message_size = 0;

    size_t bytes_received = 0;
    printf("receiving message size\n");
    size_t s_size = sizeof(uintsize_t);
    while (1) {
        ssize_t n = recv(sockfd, &message_size + bytes_received, s_size - bytes_received, 0);

        if (n < 0) {
            perror("recv");
            exit(EXIT_FAILURE);
        } else if (n == 0) {
            return n;
            break;
        } else {
            bytes_received += (size_t) n;
            if (bytes_received == s_size) {
                break;
            }
        }

    }

    message_size = ntohsize(message_size);

    return message_size;
}

static size_t recv_data(int sockfd, rpc_data *buffer) {
    int data_1 = 0;
    size_t data_2_len;
    // receiving data_1 int
    recv_int(sockfd, &data_1);
    buffer->data1 = data_1;
    printf("received data1: %d\n", buffer->data1);

    // receiving data_2 length
    data_2_len = recv_size(sockfd);
    buffer->data2_len = data_2_len;
    printf("received data2_len: %zu\n", buffer->data2_len);

    // receiving data_2
    if (data_2_len > 0) {
        buffer->data2 = malloc(data_2_len);
        assert(buffer->data2);
        size_t n = recv_void(sockfd, data_2_len, buffer->data2);
//        if (buffer->data2 != NULL) printf("data is %d\n",  *((int *) buffer->data2));
    } else {
        buffer->data2 = NULL;
        printf("no bytes read\n");
    }



    return 0;
}

static size_t recv_void(int sockfd, size_t size, void *data) {
    size_t bytes_received = 0;
    while(1) {
        ssize_t n = recv(sockfd, data + bytes_received, size - bytes_received, 0);
        if (n < 0) {
            perror("recv");
            exit(EXIT_FAILURE);
        } else if (n == 0) {
            return n;
            break;
        } else {
            bytes_received += (size_t) n;
            if (bytes_received == size) {
                break;
            }
        }
    }


    return bytes_received;
}

static size_t recv_int(int sockfd, int *data) {
    size_t bytes_received = 0;
    size_t int_size = sizeof(int);
    while (1) {
        ssize_t n = recv(sockfd, data + bytes_received, int_size - bytes_received, 0);

        if (n < 0) {
            perror("recv");
            exit(EXIT_FAILURE);
        } else if (n == 0) {
            return n;
            break;
        } else {
            bytes_received += (size_t) n;
            if (bytes_received == int_size) {
                break;
            }
        }

    }

    *data = ntohl(*data);

    return bytes_received;
}

static size_t recv_type(int sockfd, char *data) {


    ssize_t n = recv(sockfd, data, sizeof(*data), 0);

    if (n < 0) {
        perror("recv");
        exit(EXIT_FAILURE);
    } else if (n == 0) {
        return n;
    }

    return n;
}

static size_t send_type(int sockfd, char data) {

    int n = send(sockfd, &data, sizeof(data), 0);
    if (n < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
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

