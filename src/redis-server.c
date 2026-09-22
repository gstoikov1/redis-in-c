#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 6379
struct sockaddr_in address;
struct sockaddr client_address;
int opt = 1;

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
    int client_socket;
    int client_socket_size = sizeof(client_address);
    if ((client_socket =
             accept(server_socket, (struct sockaddr *)&client_address, &client_socket_size)) < 0) {
        perror("accept");
        return -1;
    }

    return 0;
}