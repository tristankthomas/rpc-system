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

// use to change depending on system
#if defined(htobe64) && !defined(htonll)
    #define htonll(x) htobe64(x)
#endif

#if defined(be64toh) && !defined(ntohll)
    #define ntohll(x) be64toh(x)
#endif

#if defined(__SIZEOF_SIZE_T__)
    #if __SIZEOF_SIZE_T__ == 4
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
    int port_num;
    struct sockaddr_in6 client_addr;
    // will have hash-table later (one handler for now)
    rpc_handler procedure;

};

struct rpc_client {
    int sockfd;
    int port_num;
    char *server_addr;

};

struct rpc_handle {
    char *name;
    int id;
};

static int send_size(size_t size, int sockfd);
static int recv_size(int sockfd);
static int send_message(char *message, int sockfd);
static int recv_message(size_t size, char *buffer, int sockfd);
static int send_data(int sockfd, rpc_data *data);
static int recv_data(int sockfd, rpc_data *buffer);
static int recv_int(int sockfd, int *data_1);
static int send_int(int sockfd, int data);
static int send_void(int sockfd, size_t size, void *data);
static int recv_void(int sockfd, size_t size, void *data);


rpc_server *rpc_init_server(int port) {

    int enable = 1, s, connectfd, listenfd, port2;
    struct addrinfo hints, *res, *p;
    struct sockaddr_in6 client_addr;
    char port_str[6];
    char ip[INET6_ADDRSTRLEN];
    socklen_t client_addr_size;

    struct rpc_server *server = malloc(sizeof(struct rpc_server));
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

    client_addr_size = sizeof client_addr;

    // accept connection from client (takes from listen queue)
    connectfd = accept(listenfd, (struct sockaddr *) &client_addr, &client_addr_size);
    if (connectfd < 0) {
        perror("accept");
        return NULL;
    }

    // Print ipv4 peer information (can be removed)
    getpeername(connectfd, (struct sockaddr *) &client_addr, &client_addr_size);
    inet_ntop(client_addr.sin6_family, &client_addr.sin6_addr, ip, INET6_ADDRSTRLEN);
    // client port
    port2 = ntohs(client_addr.sin6_port);
    printf("new connection from %s:%d on socket %d\n", ip, port2, connectfd);

    // assign to server
    server->client_addr = client_addr;
    server->port_num = port;
    server->sockfd = connectfd;

    freeaddrinfo(res);



    return server;
}

int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
    printf("SERVER: registering function %s \n", name);
    srv->procedure = handler;

    uintsize_t message_size = recv_size(srv->sockfd);
    printf("message size is %zu\n", message_size);

    char message[256];
    recv_message(message_size, message, srv->sockfd);
    printf("Here is the message: %s\n", message);


    return 1;
    //return -1;
}

void rpc_serve_all(rpc_server *srv) {
    char buffer[255];
    rpc_data data1 = {.data1 = 1, .data2_len = 0, .data2 = NULL};
    rpc_data data2 = {.data1 = 1, .data2_len = 0, .data2 = NULL};
    rpc_data *result1;
    rpc_data *result2;

    // reads function name size
//    int size = recv_size(srv->sockfd);
//    printf("function name size is %d\n", size);
//
//    // reads function name
//    recv_message(size, buffer, srv->sockfd);
//    printf("Here is the function name: %s\n", buffer);

    recv_data(srv->sockfd, &data1);
    printf("stored data is %d\n", *((int*)data1.data2));

    recv_data(srv->sockfd, &data2);
    printf("stored data is %d\n", *((int*)data2.data2));

    result1 = srv->procedure(&data1);
    printf("result1 is %d\n", result1->data1);
    result2 = srv->procedure(&data2);
    printf("result2 is %d\n", result2->data1);
//    // reads data_2_length
//    recv_size(srv->sockfd);
//    printf("received %zu\n", data2.data2_len);



}


rpc_client *rpc_init_client(char *addr, int port) {
    int connectfd, s;
    struct addrinfo hints, *servinfo, *p;
    char port_str[6];
    struct rpc_client *client = malloc(sizeof(struct rpc_client));

    assert(client);
    client->server_addr = strdup(addr);
    assert(client->server_addr);


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
    client->port_num = port;
    client->sockfd = connectfd;

    freeaddrinfo(servinfo);


    return client;
}

rpc_handle *rpc_find(rpc_client *cl, char *name) {
    printf("CLIENT: finding function %s \n", name);
    char word[] = "hello";
    int n;
    rpc_handle *handle = malloc(sizeof handle);
    assert(handle);
    handle->name = strdup(name);
    assert(handle->name);

    // Read message from stdin
//    printf("Please enter the message: ");
//    if (fgets(buffer, 255, stdin) == NULL) {
//        exit(EXIT_SUCCESS);
//    }
    // Remove \n that was read by fgets
//    buffer[strlen(buffer) - 1] = 0;

    size_t size = strlen(word);
    // sends size of message
    printf("sending size of message\n");
    send_size(size, cl->sockfd);

    // sends message to server
    printf("sending message, %s\n", word);
    send_message(word, cl->sockfd);

    return handle;
    //return NULL;
}


rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    printf("CLIENT: calling function %s \n", h->name);
    // send function name size to server
//    send_size(strlen(h->name), cl->sockfd);
//
//    // send function name to server
//    send_message(h->name, cl->sockfd);

    // send rpc_data to server
    printf("CLIENT: sending data\n");
    send_data(cl->sockfd, payload);

    return NULL;
}






// sending functions

static int send_data(int sockfd, rpc_data *data) {
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
    printf("sending data_2\n");
    send_void(sockfd, size, data_2);

    return 0;

}

static int send_void(int sockfd, size_t size, void *data) {

    int n = send(sockfd, data, size, 0);
    if (n < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    return n;

}

static int send_message(char *message, int sockfd) {

    // Send message
    int n = send(sockfd, message, strlen(message), 0);
    if (n < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    return n;
}

static int send_size(size_t size, int sockfd) {


    uintsize_t size_n = htonsize((uintsize_t) size);

    int n = send(sockfd, &size_n, sizeof(size_n), 0);
    if (n < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    return n;
}

static int send_int(int sockfd, int data) {
    int data_n = htonl(data);
    int n = send(sockfd, &data_n, sizeof(data_n), 0);
    if (n < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    return n;
}




// receiving functios
static int recv_message(size_t size, char *buffer, int sockfd) {

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




static int recv_size(int sockfd) {


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

static int recv_data(int sockfd, rpc_data *buffer) {
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
    void *data_2 = malloc(data_2_len);
    size_t num = recv_void(sockfd, data_2_len, data_2);
    printf("received data_2 of size %zu\n", num);
    printf("data is %d\n",  *((int*)data_2));
    buffer->data2 = data_2;


    return 0;
}

static int recv_void(int sockfd, size_t size, void *data) {
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

static int recv_int(int sockfd, int *data_1) {
    int bytes_received = 0;
    size_t int_size = sizeof(int);
    while (1) {
        int n = recv(sockfd, data_1 + bytes_received, int_size - bytes_received, 0);

        if (n < 0) {
            perror("recv");
            exit(EXIT_FAILURE);
        } else if (n == 0) {
            break;
        } else {
            bytes_received += n;
            if (bytes_received == int_size) {
                break;
            }
        }

    }

    *data_1 = ntohl(*data_1);

    return bytes_received;
}



void rpc_close_client(rpc_client *cl) {

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

