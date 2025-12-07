#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

volatile sig_atomic_t was_sighup = 0;

void sig_hup_handler(int signo)
{
    (void)signo;
    was_sighup = 1;   // просто помечаем, что сигнал был
}

int create_listen_socket(int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        close(sock);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (listen(sock, 10) == -1) {
        perror("listen");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);
    return sock;
}

int main(void)
{
    const int PORT = 12345;

    int listen_fd = create_listen_socket(PORT);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_hup_handler;
    sa.sa_flags   = SA_RESTART; 
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    sigset_t blockedMask, origMask;
    sigemptyset(&blockedMask);
    sigaddset(&blockedMask, SIGHUP);

    if (sigprocmask(SIG_BLOCK, &blockedMask, &origMask) == -1) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    int client_fd = -1;
    int max_fd    = listen_fd;

    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);
        if (client_fd != -1) {
            FD_SET(client_fd, &readfds);
            if (client_fd > max_fd)
                max_fd = client_fd;
        } else {
            max_fd = listen_fd;
        }

        int ready = pselect(max_fd + 1, &readfds, NULL, NULL, NULL, &origMask);
        if (ready == -1) {
            if (errno == EINTR) {
                if (was_sighup) {
                    printf("Signal SIGHUP received\n");
                    was_sighup = 0;
                }
                continue;
            } else {
                perror("pselect");
                break;
            }
        }

        if (FD_ISSET(listen_fd, &readfds)) {
            struct sockaddr_in cliaddr;
            socklen_t          clilen = sizeof(cliaddr);
            int new_fd = accept(listen_fd, (struct sockaddr *)&cliaddr, &clilen);
            if (new_fd == -1) {
                perror("accept");
            } else {
                char addr_str[64];
                inet_ntop(AF_INET, &cliaddr.sin_addr, addr_str, sizeof(addr_str));
                printf("New connection from %s:%d\n",
                       addr_str, ntohs(cliaddr.sin_port));

                if (client_fd == -1) {
                    client_fd = new_fd;
                    printf("Connection accepted (fd=%d)\n", client_fd);
                } else {
                    printf("Extra connection closed immediately (fd=%d)\n", new_fd);
                    close(new_fd);
                }
            }
        }

        if (client_fd != -1 && FD_ISSET(client_fd, &readfds)) {
            char buf[4096];
            ssize_t nread = read(client_fd, buf, sizeof(buf));
            if (nread > 0) {
                printf("Received %zd bytes from client\n", nread);
            } else if (nread == 0) {
                printf("Client disconnected (fd=%d)\n", client_fd);
                close(client_fd);
                client_fd = -1;
            } else {
                perror("read");
                close(client_fd);
                client_fd = -1;
            }
        }

        if (was_sighup) {
            printf("Signal SIGHUP received (post pselect)\n");
            was_sighup = 0;
        }
    }

    if (client_fd != -1)
        close(client_fd);
    close(listen_fd);
    return 0;
}
