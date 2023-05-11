#include "rpc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>


struct rpc_server {
    int sockfd;
    int port_num;
    struct sockaddr_in6 client_addr;

};

rpc_server *rpc_init_server(int port) {

    int enable = 1, s, connectfd, listenfd, port2;
    struct addrinfo hints, *res, *p;
    struct sockaddr_in6 client_addr;
    char port_str[6];
    char buffer[256], ip[INET6_ADDRSTRLEN];
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
    return -1;
}

void rpc_serve_all(rpc_server *srv) {

}

struct rpc_client {
    int sockfd;
    int port_num;
    char *server_addr;

};

struct rpc_handle {
    /* Add variable(s) for handle */
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


    // Read message from stdin
//    printf("Please enter the message: ");
//    if (fgets(buffer, 255, stdin) == NULL) {
//        exit(EXIT_SUCCESS);
//    }
//    // Remove \n that was read by fgets
//    buffer[strlen(buffer) - 1] = 0;
//
//    // Send message to server
//    n = write(sockfd, buffer, strlen(buffer));
//    if (n < 0) {
//        perror("socket");
//        exit(EXIT_FAILURE);
//    }

    return client;
}

rpc_handle *rpc_find(rpc_client *cl, char *name) {
    return NULL;
}

rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
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
