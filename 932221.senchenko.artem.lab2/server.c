// _GNU_SOURCE нужен для корректного объявления pselect в glibc
#define _GNU_SOURCE

#include <stdio.h>      // printf, perror
#include <stdlib.h>     // exit, EXIT_FAILURE
#include <string.h>     // memset
#include <errno.h>      // errno, EINTR
#include <unistd.h>     // read, close
#include <signal.h>     // sigaction, sigprocmask, сигналы

// Блоки работы с сокетами
#include <sys/types.h>
#include <sys/socket.h> // socket, bind, listen, accept
#include <netinet/in.h> // struct sockaddr_in
#include <arpa/inet.h>  // inet_ntop для печати IP адреса

//--------------------------------------------------------
//  ГЛОБАЛЬНЫЙ ФЛАГ, КОТОРЫЙ СТАВИТСЯ В ОБРАБОТЧИКЕ СИГНАЛА
//--------------------------------------------------------
// volatile + sig_atomic_t = безопасно менять внутри signal handler
volatile sig_atomic_t was_sighup = 0;

//--------------------------------------------------------
//  ОБРАБОТЧИК СИГНАЛА SIGHUP — делает минимум: ставит флаг
//--------------------------------------------------------
void sig_hup_handler(int signo)
{
    (void)signo;        // игнорируем аргумент
    was_sighup = 1;     // помечаем, что сигнал пришёл
}

//--------------------------------------------------------
//   СОЗДАЁМ TCP-СЕРВЕРНЫЙ LISTEN СОКЕТ
//--------------------------------------------------------
int create_listen_socket(int port)
{
    // AF_INET = IPv4, SOCK_STREAM = TCP
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Разрешаем порт переиспользовать сразу после перезапуска программы
    int optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Заполняем структуру адреса
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;            // IPv4
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // слушаем на всех интерфейсах
    addr.sin_port        = htons(port);        // порт, в сетевом порядке байт

    // Привязка сокета к адресу
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Перевод в режим прослушивания
    if (listen(sock, 10) == -1) {
        perror("listen");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);
    return sock;
}

//--------------------------------------------------------
//                    MAIN
//--------------------------------------------------------
int main(void)
{
    const int PORT = 12345;

    // Создаём слушающий сокет
    int listen_fd = create_listen_socket(PORT);

    //----------------------------------------------------
    //      НАСТРОЙКА ОБРАБОТЧИКА СИГНАЛА SIGHUP
    //----------------------------------------------------
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_hup_handler;    // указываем обработчик
    sa.sa_flags   = SA_RESTART;         // некоторые системные вызовы будут перезапущены
    sigemptyset(&sa.sa_mask);           // внутри обработчика ничего не блокируем

    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    //----------------------------------------------------
    //      БЛОКИРУЕМ SIGHUP ПО-УМОЛЧАНИЮ
    //  ВАЖНО: pselect временно разблокирует его и избежит race condition
    //----------------------------------------------------
    sigset_t blockedMask, origMask;
    sigemptyset(&blockedMask);
    sigaddset(&blockedMask, SIGHUP);          // добавили SIGHUP в маску блокировки

    if (sigprocmask(SIG_BLOCK, &blockedMask, &origMask) == -1) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    //----------------------------------------------------
    //     ТОЛЬКО ОДИН КЛИЕНТ СОЕДИНЁН ОДНОВРЕМЕННО
    //----------------------------------------------------
    int client_fd = -1;
    int max_fd    = listen_fd;

    //----------------------------------------------------
    //                   ГЛАВНЫЙ ЦИКЛ
    //----------------------------------------------------
    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);  // слушающий сокет: новое подключение?

        // Если есть клиент — следим и за его сокетом
        if (client_fd != -1) {
            FD_SET(client_fd, &readfds);
            if (client_fd > max_fd)
                max_fd = client_fd;
        } else {
            max_fd = listen_fd;
        }

        //------------------------------------------------
        //                  ВЫЗОВ PSELECT
        //------------------------------------------------
        // pselect блокирует процесс И одновременно ожидает сигналы
        // origMask = маска сигналов, которая действует во время ожидания
        //------------------------------------------------
        int ready = pselect(max_fd + 1, &readfds, NULL, NULL, NULL, &origMask);

        if (ready == -1) {
            // EINTR — вызов прерван сигналом
            if (errno == EINTR) {
                if (was_sighup) {
                    printf("Signal SIGHUP received\n");
                    was_sighup = 0;
                }
                continue;  // продолжаем цикл
            } else {
                perror("pselect");
                break;
            }
        }

        //------------------------------------------------
        //      ЕСЛИ ПРИШЛО НОВОЕ ПОДКЛЮЧЕНИЕ (accept)
        //------------------------------------------------
        if (FD_ISSET(listen_fd, &readfds)) {
            struct sockaddr_in cliaddr;
            socklen_t clilen = sizeof(cliaddr);
            int new_fd = accept(listen_fd, (struct sockaddr *)&cliaddr, &clilen);

            if (new_fd == -1) {
                perror("accept");
            } else {
                char addr_str[64];
                inet_ntop(AF_INET, &cliaddr.sin_addr, addr_str, sizeof(addr_str));
                printf("New connection from %s:%d\n",
                       addr_str, ntohs(cliaddr.sin_port));

                // Если у нас ещё нет клиента — принимаем
                if (client_fd == -1) {
                    client_fd = new_fd;
                    printf("Connection accepted (fd=%d)\n", client_fd);
                }
                // Иначе — сразу закрываем (так требует задание)
                else {
                    printf("Extra connection closed immediately (fd=%d)\n", new_fd);
                    close(new_fd);
                }
            }
        }

        //------------------------------------------------
        //      ЕСЛИ ПРИШЛИ ДАННЫЕ ОТ КЛИЕНТА
        //------------------------------------------------
        if (client_fd != -1 && FD_ISSET(client_fd, &readfds)) {
            char buf[4096];
            ssize_t nread = read(client_fd, buf, sizeof(buf));

            if (nread > 0) {
                printf("Received %zd bytes from client\n", nread);
            }
            else if (nread == 0) {
                printf("Client disconnected (fd=%d)\n", client_fd);
                close(client_fd);
                client_fd = -1;
            }
            else {
                perror("read");
                close(client_fd);
                client_fd = -1;
            }
        }

        //------------------------------------------------
        //   ЕСЛИ СИГНАЛ ПРИШЁЛ ПОСЛЕ PSELECT (редко)
        //------------------------------------------------
        if (was_sighup) {
            printf("Signal SIGHUP received (post pselect)\n");
            was_sighup = 0;
        }
    }

    //------------------------------------------------
    //         АККУРАТНОЕ ЗАВЕРШЕНИЕ
    //------------------------------------------------
    if (client_fd != -1)
        close(client_fd);
    close(listen_fd);
    return 0;
}

// послать сигнал можно так kill -HUP <PID_твоего_сервера>


/*
❓ Что такое sigprocmask и зачем блокировать сигналы?

Ответ:
sigprocmask меняет маску сигналов — то есть говорит ядру, какие сигналы временно игнорировать (не доставлять).

Мы блокируем сигнал, чтобы:

сигнал НЕ прерывал важный системный вызов (например, accept/read),

сигнал доставлялся только в контролируемый момент,

не было race condition между “я только что вызвал select” и “сигнал пришёл до того, как select реально начал ждать”.

Это классическая техника безопасности в многособытийных серверах.
 */


 /*
❓ Что такое pselect и зачем он нужен?

Ответ:
pselect — улучшенная версия select.
Она решает главную проблему обычного select: race condition между sigprocmask и вызовом select.

pselect делает три вещи атомарно:

применяет новую маску сигналов (например, разблокирует SIGHUP),

начинает ждать события на сокетах,

возвращает маску обратно после выхода.

То есть signal → либо приходит прямо во время pselect, либо после.
Никаких дыр между вызовами.
 */

 /*
 ❓ Почему передаём origMask в pselect?

Ответ:
origMask — маска сигналов, при которой SIGHUP разблокирован.
Значит во время pselect сигнал может прийти и корректно прервать вызов.

После возврата pselect сам восстановит предыдущую маску (где SIGHUP снова блокирован).
 */