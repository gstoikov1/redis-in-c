#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 6379
#define BUF_SIZE 4096

struct sockaddr_in address;
struct sockaddr client_address;
int opt = 1;
char buf[BUF_SIZE] = {0};
char queried_data[BUF_SIZE] = {0};

#define MAX_EVENTS 10
struct epoll_event ev, events[MAX_EVENTS];
int conn_sock, nfds, epollfd;

enum request_result {
    SUCCESS,
    NEED_MORE_DATA
};

struct Client {
    int ptr;
    char buffer[4096];
    int buffered;
};

enum request_result check_request(const char *req, int size, int *consumed);
void get_request_from_buffer(const char *buff, int consumed, int buffered, char *dest);

#define REQUEST_LENGTH 14 // len("*1$\r\n4\r\nPING\r\n")

struct request {};

int main() {
    printf("Hello, World!\n");

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket <= 0) {
        perror("socket");
        return -1;
    }

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_socket);
        return -1;
    }
    if (listen(server_socket, 0)) {
        perror("listen");
        return -1;
    };
    int client_socket = 0;
    int client_socket_size = sizeof(client_address);

    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create1");
        exit(1);
    }
    ev.events = EPOLLIN;
    ev.data.fd = server_socket;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server_socket, &ev) == -1) {
        perror("epoll_ctl: server_socket");
        exit(1);
    }

    for (;;) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(1);
        }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == server_socket) {
                conn_sock =
                    accept(server_socket, (struct sockaddr *)&client_address, &client_socket_size);
                if (conn_sock == -1) {
                    perror("accept");
                    exit(1);
                }
                struct Client *client = malloc(sizeof(struct Client));
                client->ptr = conn_sock;
                client->buffered = 0;
                ev.events = EPOLLIN;
                ev.data.ptr = client;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
                    perror("epoll_ctl: conn_sock");
                    exit(1);
                }
                printf("DEBUG: Client Connected\n");
            } else {
                struct Client *client = (struct Client *)events[n].data.ptr;
                int rec_bytes = recv(client->ptr, client->buffer + client->buffered,
                                     sizeof(client->buffer) - client->buffered, 0);
                client->buffered += rec_bytes;

                while (1) {
                    int consumed = 0;
                    enum request_result res = check_request(buf, client->buffered, &consumed);
                    if (res == NEED_MORE_DATA) {
                        break;
                    }
                    get_request_from_buffer(&buf, consumed, client->buffered, queried_data);
                    client->buffered -= consumed;

                    if (send(client->ptr, "+PONG\r\n", 7, 0) < 0) {
                        perror("send");
                        close(client_socket);
                        break;
                    }
                }
            }
        }
    }

    return 0;
}

// placeholder function to check if we have a full request already pending in req
enum request_result check_request(const char *req, int size, int *req_len) {
    if (size < REQUEST_LENGTH) {
        return NEED_MORE_DATA;
    }
    *req_len = REQUEST_LENGTH;
    return SUCCESS;
}

void get_request_from_buffer(const char *buff, int req_length, int buff_size, char *dest) {
    assert(buff_size >= req_length);
    strncpy(dest, buf, req_length);
    printf("strlen %d\n", strlen(buff));
    printf("dest %s\n", dest);
    memmove(buf, buf + req_length, buff_size - req_length);
}