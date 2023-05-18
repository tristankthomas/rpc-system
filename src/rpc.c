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


struct rpc_server {
    int sockfd;
    int listenfd;
    // will have hash-table later (one handler for now)
    hash_table_t *procedures;

};

struct rpc_client {
    int sockfd;
};

struct rpc_handle {
    int id;
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
    };

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
        exit(EXIT_FAILURE);
    }


    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // bind
    if (bind(listenfd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    // listen (blocking)
    if (listen(listenfd, 5) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }


    // assign to server
    server->listenfd = listenfd;

    freeaddrinfo(res);

    server->procedures = create_empty_table();


    return server;
}

int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {

    insert_data(srv->procedures, name, (void *) handler, (hash_func) hash_djb2);
    printf("%s function registered\n", name);
    return 1;

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




    rpc_data *data1 = malloc(sizeof(*data1));
    assert(data1);
    rpc_data *data2 = malloc(sizeof(*data2));
    assert(data2);

    rpc_data *result1;
    rpc_data *result2;

    // reads function name size
    int size = recv_size(srv->sockfd);
    printf("function name size is %d\n", size);

    // reads function name
    recv_string(size, name, srv->sockfd);
    printf("Here is the function name: %s\n", name);
    rpc_handler handler = (rpc_handler) get_data(srv->procedures, name, (hash_func) hash_djb2, (compare_func) strcmp);



    recv_data(srv->sockfd, data1);
//    if (data1.data2 != NULL) printf("stored data is %d\n", *((int*)data1.data2));

    result1 = ((rpc_handler) get_data(srv->procedures, name, (hash_func) hash_djb2, (compare_func) strcmp))(data1);

    rpc_data_free(data1);
    printf("result1 is %d\n", result1->data1);

    send_data(srv->sockfd, result1);

    rpc_data_free(result1);


    recv_data(srv->sockfd, data2);
 //   if (data2.data2 != NULL) printf("stored data is %d\n", *((int*)data2.data2));


    result2 = ((rpc_handler) get_data(srv->procedures, name, (hash_func)hash_djb2, (compare_func) strcmp))(data2);

    rpc_data_free(data2);
    printf("result2 is %d\n", result2->data1);

    send_data(srv->sockfd, result2);

    rpc_data_free(result2);







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
        exit(EXIT_FAILURE);
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

rpc_handle *rpc_find(rpc_client *cl, char *name) {
    //printf("CLIENT: finding function %s \n", name);
    rpc_handle *handle = malloc(sizeof(*handle));
    assert(handle);
    handle->id = 0;

    // send function name size to server
    send_size(strlen(name), cl->sockfd);

    // send function name to server
    send_string(name, cl->sockfd);

    return handle;
    //return NULL;
}


rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    //printf("CLIENT: calling function %s \n", h->name);
    rpc_data *result = malloc(sizeof(*result));
    assert(result);
    // send function name size to server
//    send_size(strlen(h->name), cl->sockfd);
//
//    // send function name to server
//    send_string(h->name, cl->sockfd);

    // send rpc_data to server
    printf("CLIENT: sending data\n");
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
            break;
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
        size_t num = recv_void(sockfd, data_2_len, buffer->data2);
        printf("received data_2 of size %zu\n", num);
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


// hash functions
static uint32_t hash_djb2(char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}


void rpc_close_client(rpc_client *cl) {
    free(cl);
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

