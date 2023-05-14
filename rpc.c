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


struct rpc_server {
    int sockfd;
    int port_num;
    struct sockaddr_in6 client_addr;
    // will have hash-table later (one handler for now)
    rpc_handler procedure;

};

static int send_message_size(int size, int sockfd);
static int recv_message_size(int sockfd);
static int send_message(char *message, int sockfd);
static int recv_message(int size, char *buffer, int sockfd);


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

    int message_size = recv_message_size(srv->sockfd);
    printf("message size is %d\n", message_size);

    char message[256];
    recv_message(message_size, message, srv->sockfd);
    printf("Here is the message: %s\n", message);


    return 1;
    //return -1;
}

void rpc_serve_all(rpc_server *srv) {
    char buffer[255];

    // reads function name size
    int size = recv_message_size(srv->sockfd);
    printf("function name size is %d\n", size);

    // reads function name
    recv_message(size, buffer, srv->sockfd);
    printf("Here is the function name: %s\n", buffer);


}

struct rpc_client {
    int sockfd;
    int port_num;
    char *server_addr;

};

struct rpc_handle {
    char *name;
    int id;
};

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
    char buffer[256];
    int n;
    rpc_handle *handle = malloc(sizeof handle);
    assert(handle);
    handle->name = strdup(name);
    assert(handle->name);

    // Read message from stdin
    printf("Please enter the message: ");
    if (fgets(buffer, 255, stdin) == NULL) {
        exit(EXIT_SUCCESS);
    }
    // Remove \n that was read by fgets
    buffer[strlen(buffer) - 1] = 0;

    int size = strlen(buffer);
    // sends size of message
    send_message_size(size, cl->sockfd);

    // sends message to server
    send_message(buffer, cl->sockfd);

    return handle;
    //return NULL;
}

static int send_message(char *message, int sockfd) {
    printf("sending message, %s\n", message);
    // Send message
    int n = send(sockfd, message, strlen(message), 0);
    if (n < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    return n;
}

static int recv_message(int size, char *buffer, int sockfd) {

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


static int send_message_size(int size, int sockfd) {
    int size_n = htonl(size);
    printf("sending size of message\n");
    int n = send(sockfd, &size_n, sizeof(size_n), 0);
    if (n < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    return n;
}

static int recv_message_size(int sockfd) {
    int message_size = 0;
    int bytes_received = 0;
    printf("receiving message size\n");
    size_t int_size = sizeof(int);
    while (1) {
        int n = recv(sockfd, &message_size + bytes_received, int_size - bytes_received, 0);

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

    message_size = ntohl(message_size);

    return message_size;
}

rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    printf("CLIENT: calling function %s \n", h->name);
    // send function name size to server
    send_message_size(strlen(h->name), cl->sockfd);

    // send function name to server
    send_message(h->name, cl->sockfd);

    return NULL;
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
