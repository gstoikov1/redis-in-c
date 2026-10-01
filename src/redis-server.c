#define _GNU_SOURCE // needed for memmem

#include "hash_table.h"
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
Table table;

#define MAX_EVENTS 10
struct epoll_event ev, events[MAX_EVENTS];
int conn_sock, nfds, epollfd;

enum request_result {
    SUCCESS,
    NEED_MORE_DATA,
    INVALLID
};
enum Command {
    ECHO,
    PING,
    SET
};

struct Client {
    int fd;
    char buffer[4096];
    int buffered;
};

typedef struct ArgumentNode {
    char *arg_val;
    int size;

    struct ArgumentNode *next;
} ArgumentNode;

struct RequestArguments {
    struct ArgumentNode *head;
};

enum request_result check_request(const char *req, int size, int *consumed);
void get_request_from_buffer(char *buff, int consumed, int buffered, char *dest);
int convert_string_to_number(const char *start, const char *end, int *res);
int parse_request(const char *req, int length, struct RequestArguments *request_arguments);
void add_to_args(struct RequestArguments *request_args, struct ArgumentNode *node);
void print_args(struct RequestArguments *request_args);
int handle_request(int client_fd, const struct RequestArguments *args);

int main() {
    if (init(&table, 1024)) {
        return -1;
    }

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

    socklen_t client_socket_size = sizeof(client_address);

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
                client->fd = conn_sock;
                client->buffered = 0;
                ev.events = EPOLLIN;
                ev.data.ptr = client;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
                    perror("epoll_ctl: conn_sock");
                    exit(1);
                }

            } else {
                struct Client *client = (struct Client *)events[n].data.ptr;
                int rec_bytes = recv(client->fd, client->buffer + client->buffered,
                                     sizeof(client->buffer) - client->buffered, 0);
                client->buffered += rec_bytes;
                while (1) {
                    int consumed = 0;
                    enum request_result res =
                        check_request(client->buffer, client->buffered, &consumed);
                    if (res == NEED_MORE_DATA || res == INVALLID) {
                        break;
                    }
                    get_request_from_buffer(client->buffer, consumed, client->buffered,
                                            queried_data);

                    client->buffered -= consumed;
                    struct RequestArguments arguments = {.head = NULL};

                    parse_request(queried_data, consumed, &arguments);
                    handle_request(client->fd, &arguments);
                    print_args(&arguments);
                }
            }
        }
    }

    return 0;
}

enum request_result check_request(const char *req, int size, int *consumed) {
    int pos = 0;

    if (size < 1) {
        return NEED_MORE_DATA;
    }

    if (req[pos] != '*') {
        return INVALLID;
    }

    pos++;

    const char *header_end = memmem(req + pos, size - pos, "\r\n", 2);

    if (!header_end) {
        return NEED_MORE_DATA;
    }

    int array_len;

    if (convert_string_to_number(req + pos, header_end - 1, &array_len) != 0) {
        return INVALLID;
    }

    pos = header_end - req + 2;

    for (int i = 0; i < array_len; i++) {

        /* Need at least "$...\r\n" */
        if (pos >= size) {
            return NEED_MORE_DATA;
        }

        if (req[pos] != '$') {
            return INVALLID;
        }

        pos++;

        const char *length_end = memmem(req + pos, size - pos, "\r\n", 2);

        if (!length_end) {
            return NEED_MORE_DATA;
        }

        int len;

        if (convert_string_to_number(req + pos, length_end - 1, &len) != 0) {
            return INVALLID;
        }

        /* Move to first byte of bulk string data */
        pos = length_end - req + 2;

        /*
         * Need:
         *
         * len bytes of value
         * +
         * \r\n
         */
        if (pos + len + 2 > size) {
            return NEED_MORE_DATA;
        }

        /* Verify trailing CRLF */
        if (req[pos + len] != '\r' || req[pos + len + 1] != '\n') {
            return INVALLID;
        }

        /*
         * Skip:
         * data + \r\n
         */
        pos += len + 2;
    }

    *consumed = pos;

    return SUCCESS;
}

void get_request_from_buffer(char *buff, int req_length, int buff_size, char *dest) {
    assert(buff_size >= req_length);
    assert(BUF_SIZE > req_length);

    strncpy(dest, buff, req_length);
    memmove(buff, buff + req_length, buff_size - req_length);
    dest[req_length] = '\0';
}

int convert_string_to_number(const char *start, const char *end, int *res) {
    int val = 0;
    while (start <= end) {
        if (*start < '0' || *start > '9') {
            return -1;
        }
        val = val * 10 + (*start - '0');
        start++;
    }
    *res = val;

    return 0;
}

int parse_request(const char *req, int request_len, struct RequestArguments *request_arguments) {
    int pos = 0;

    if (request_len < 1 || req[pos] != '*') {
        return -1;
    }

    pos++;

    const char *header_end = memmem(req + pos, request_len - pos, "\r\n", 2);

    if (!header_end) {
        return -1;
    }

    int array_len;

    if (convert_string_to_number(req + pos, header_end - 1, &array_len) != 0) {
        return -1;
    }

    pos = header_end - req + 2;

    for (int i = 0; i < array_len; i++) {
        if (pos >= request_len || req[pos] != '$') {
            return -1;
        }

        pos++;

        const char *length_end = memmem(req + pos, request_len - pos, "\r\n", 2);

        if (!length_end) {
            return -1;
        }

        int arg_len;

        if (convert_string_to_number(req + pos, length_end - 1, &arg_len) != 0) {
            return -1;
        }

        pos = length_end - req + 2;

        if (pos + arg_len + 2 > request_len) {
            return -1;
        }

        if (req[pos + arg_len] != '\r' || req[pos + arg_len + 1] != '\n') {
            return -1;
        }

        struct ArgumentNode *node = malloc(sizeof *node);
        node->arg_val = malloc(arg_len + 1);

        memcpy(node->arg_val, req + pos, arg_len);
        node->arg_val[arg_len] = '\0';

        node->size = arg_len;
        node->next = NULL;

        add_to_args(request_arguments, node);

        pos += arg_len + 2;
    }

    return 0;
}

void add_to_args(struct RequestArguments *request_args, struct ArgumentNode *node) {
    if (request_args->head == NULL) {
        request_args->head = node;
        return;
    }
    struct ArgumentNode *current_node = request_args->head;
    while (current_node->next != NULL) {
        current_node = current_node->next;
    }
    current_node->next = node;
}

void print_args(struct RequestArguments *request_args) {
    printf("ARGS BEGIN\n");

    struct ArgumentNode *curr = request_args->head;
    int i = 1;

    while (curr) {
        printf("%d. %s of length %d\n", i, curr->arg_val, curr->size);

        curr = curr->next;
        i++;
    }

    printf("ARGS END\n");
}

int handle_request(int client_fd, const struct RequestArguments *args) {
    struct ArgumentNode *command_node = args->head;

    const char *command_name = command_node->arg_val;
    int command_len = command_node->size;
    if (command_len >= 4 && strncmp(command_name, "PING", 4) == 0) {
        if (send(client_fd, "+PONG\r\n", 7, 0) < 0) {
            perror("send");
            return -1;
        }
    } else if (command_len >= 4 && strncmp(command_name, "ECHO", 4) == 0 &&
               command_node->next != NULL) {
        struct ArgumentNode *echo_arg = command_node->next;

        char header[64];
        int header_len = snprintf(header, sizeof(header), "$%d\r\n", echo_arg->size);
        send(client_fd, header, header_len, 0);
        send(client_fd, echo_arg->arg_val, echo_arg->size, 0);
        send(client_fd, "\r\n", 2, 0);
    } else if (command_len >= 3 && strncmp(command_name, "SET", 3) == 0) {
        struct ArgumentNode *name_node = command_node->next;
        if (name_node == NULL) {
            return -1;
        }
        struct ArgumentNode *value_node = name_node->next;
        if (value_node == NULL) {
            return -1;
        }

        if (add(&table, name_node->arg_val, name_node->size, value_node->arg_val,
                value_node->size) == 0) {
            send(client_fd, "+OK\r\n", 5, 0);
        }
    } else if (command_len >= 3 && strncmp(command_name, "GET", 3) == 0 &&
               command_node->next != NULL) {
        ArgumentNode *name_node = command_node->next;
        Entry *e = get(&table, name_node->arg_val, name_node->size);
        if (e) {
            char header[1024];
            int header_len = snprintf(header, sizeof(header), "$%d\r\n", e->value_len);
            send(client_fd, header, header_len, 0);
            send(client_fd, e->value, e->value_len, 0);
            send(client_fd, "\r\n", 2, 0);
        } else {
            send(client_fd, "$-1\r\n", 5, 0);
        }
    }

    return 0;
}