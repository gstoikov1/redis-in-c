#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 6379
#define BUF_SIZE 4096

struct sockaddr_in address;
struct sockaddr client_address;
int opt = 1;
char buf[BUF_SIZE] = {0};
char queried_data[BUF_SIZE] = {0};

enum request_result {
    SUCCESS,
    NEED_MORE_DATA
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

    while (1) {

        printf("DEBUG: Connection Open\n");
        int rec_bytes = 0;
        int buffered = 0;

        while (1) {

            if ((client_socket = accept(server_socket, (struct sockaddr *)&client_address,
                                        &client_socket_size)) < 0) {
                perror("accept");
                close(server_socket);
                return -1;
            }

            rec_bytes = recv(client_socket, buf, sizeof(buf), 0);
            if (rec_bytes < 0) {
                perror("recv");
                close(client_socket);
                break;
            }

            if (rec_bytes == 0) {
                close(client_socket);
                continue;
            }
            buffered += rec_bytes;
            while (1) {
                int consumed = 0;
                enum request_result res = check_request(buf, buffered, &consumed);
                if (res == NEED_MORE_DATA) {
                    break;
                }
                get_request_from_buffer(&buf, consumed, buffered, queried_data);
                buffered -= consumed;

                if (send(client_socket, "+PONG\r\n", 7, 0) < 0) {
                    perror("send");
                    close(client_socket);
                    break;
                }
            }
        }

        close(client_socket);
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