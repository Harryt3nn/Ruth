#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include "store.h"

#define PORT        6379
#define BACKLOG     16
#define BUF_SIZE    4096

static int server_fd = -1;

// ─── Protocol handler — one per connected client ──────────────────────────────
static void handle_command(int fd, const char *line) {
    char op[16], key[256], value[1024];
    char response[2048];

    if (sscanf(line, "PUT %255s %1023[^\n]", key, value) == 2) {
        store_put(key, value);
        write(fd, "OK\n", 3);

    } else if (sscanf(line, "GET %255s", key) == 1) {
        const char *val = store_get(key);
        if (val) {
            snprintf(response, sizeof(response), "VALUE %s\n", val);
            write(fd, response, strlen(response));
        } else {
            write(fd, "NIL\n", 4);
        }

    } else if (sscanf(line, "DEL %255s", key) == 1) {
        store_delete(key);
        write(fd, "OK\n", 3);

    } else if (strncmp(line, "KEYS", 4) == 0) {
        char *keys = store_keys();
        snprintf(response, sizeof(response), "KEYS\n%s\n", keys);
        write(fd, response, strlen(response));
        free(keys);

    } else if (strncmp(line, "PING", 4) == 0) {
        write(fd, "PONG\n", 5);

    } else {
        snprintf(response, sizeof(response), "ERROR unknown command\n");
        write(fd, response, strlen(response));
    }
}

// ─── Per-client thread ────────────────────────────────────────────────────────
static void *client_thread(void *arg) {
    int fd = *(int*)arg;
    free(arg);

    char buf[BUF_SIZE];
    char line[BUF_SIZE];
    int  line_len = 0;

    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                line[line_len] = '\0';
                if (line_len > 0) handle_command(fd, line);
                line_len = 0;
            } else if (line_len < BUF_SIZE - 1) {
                line[line_len++] = buf[i];
            }
        }
    }

    printf("[server] client disconnected (fd=%d)\n", fd);
    close(fd);
    return NULL;
}

// ─── Graceful shutdown on Ctrl+C ─────────────────────────────────────────────
static void handle_sigint(int sig) {
    (void)sig;
    printf("\n[server] shutting down...\n");
    if (server_fd != -1) close(server_fd);
    store_shutdown();
    exit(0);
}

// ─── Password prompt ──────────────────────────────────────────────────────────
static void read_password(char *buf, int maxlen) {
    struct termios old, noecho;
    tcgetattr(STDIN_FILENO, &old);
    noecho = old;
    noecho.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &noecho);
    fgets(buf, maxlen, stdin);
    buf[strcspn(buf, "\n")] = '\0';
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    printf("\n");
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main() {
    signal(SIGINT, handle_sigint);

    char password[128];
    printf("Enter store password: ");
    fflush(stdout);
    read_password(password, sizeof(password));
    if (strlen(password) == 0) { fprintf(stderr, "Error: empty password\n"); return 1; }

    store_init(password);
    memset(password, 0, sizeof(password));

    // ── TCP socket setup ──
    struct sockaddr_in addr;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    listen(server_fd, BACKLOG);
    printf("[server] listening on port %d\n", PORT);

    // ── Accept loop ──
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (client_fd < 0) break;

        printf("[server] client connected: %s\n", inet_ntoa(client_addr.sin_addr));

        int *fd_ptr = malloc(sizeof(int));
        *fd_ptr = client_fd;

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&tid, &attr, client_thread, fd_ptr);
        pthread_attr_destroy(&attr);
    }

    store_shutdown();
    return 0;
}