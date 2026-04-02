#include "store.h"
#include "protocol.h"
#include "crypto.h"      // <-- make sure this is included for Curve25519 + session
#include "session.h"     // <-- your session struct + helpers

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT     6379
#define BACKLOG  16
#define BUF_SIZE 4096

static int server_fd = -1;

// ───────────────────────────────────────────────────────────────
// Per‑client session state
// ───────────────────────────────────────────────────────────────
typedef struct {
    int     fd;
    Session session;
    int     handshook;  // 1 once HELLO exchange is done
} ClientState;

// ───────────────────────────────────────────────────────────────
// Updated dispatch() with HELLO + ROTATE + session key derivation
// ───────────────────────────────────────────────────────────────
static void dispatch(ClientState *cs, const Command *cmd) {
    int fd = cs->fd;

    // Before handshake, only HELLO is allowed
    if (!cs->handshook && cmd->type != CMD_HELLO) {
        protocol_send_response(fd, RESP_ERROR, "send HELLO first");
        return;
    }

    switch (cmd->type) {

        case CMD_HELLO: {
            uint8_t client_pub[CURVE25519_KEY_LEN];

            if (!crypto_from_hex(cmd->pubkey_hex, client_pub, CURVE25519_KEY_LEN)) {
                protocol_send_response(fd, RESP_ERROR, "bad pubkey");
                return;
            }

            if (!session_init(&cs->session)) {
                protocol_send_response(fd, RESP_ERROR, "keygen failed");
                return;
            }

            if (!crypto_derive_session(
                    cs->session.keypair.priv,
                    client_pub,
                    &cs->session.key)) {
                protocol_send_response(fd, RESP_ERROR, "key derivation failed");
                return;
            }

            char server_pub_hex[65];
            crypto_to_hex(cs->session.keypair.pub, CURVE25519_KEY_LEN, server_pub_hex);

            protocol_send_response(fd, RESP_HELLO, server_pub_hex);
            cs->handshook = 1;

            printf("[server] handshake complete (fd=%d)\n", fd);
            break;
        }

        case CMD_ROTATE: {
            session_invalidate(&cs->session);
            cs->handshook = 0;
            protocol_send_response(fd, RESP_OK, NULL);
            printf("[server] key rotation initiated (fd=%d)\n", fd);
            break;
        }

        case CMD_PUT:
            store_put(cmd->key, cmd->value);
            protocol_send_response(fd, RESP_OK, NULL);
            break;

        case CMD_GET: {
            const char *val = store_get(cmd->key);
            if (val) protocol_send_response(fd, RESP_VALUE, val);
            else     protocol_send_response(fd, RESP_NIL, NULL);
            break;
        }

        case CMD_DEL:
            store_delete(cmd->key);
            protocol_send_response(fd, RESP_OK, NULL);
            break;

        case CMD_KEYS: {
            char *keys = store_keys();
            protocol_send_response(fd, RESP_KEYS, keys);
            free(keys);
            break;
        }

        case CMD_PING:
            protocol_send_response(fd, RESP_PONG, NULL);
            break;

        case CMD_UNKNOWN:
            protocol_send_response(fd, RESP_ERROR, "unknown command");
            break;
    }
}

// ───────────────────────────────────────────────────────────────
// Updated client_thread() using ClientState
// ───────────────────────────────────────────────────────────────
static void *client_thread(void *arg) {
    int fd = *(int*)arg;
    free(arg);

    ClientState cs;
    memset(&cs, 0, sizeof(cs));
    cs.fd        = fd;
    cs.handshook = 0;

    char    buf[BUF_SIZE];
    char    line[BUF_SIZE];
    int     line_len = 0;
    ssize_t n;
    Command cmd;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                line[line_len] = '\0';
                if (line_len > 0) {
                    protocol_parse_command(line, &cmd);
                    dispatch(&cs, &cmd);
                }
                line_len = 0;
            } else if (line_len < BUF_SIZE - 1) {
                line[line_len++] = buf[i];
            }
        }
    }

    session_invalidate(&cs.session);
    printf("[server] client disconnected (fd=%d)\n", fd);
    close(fd);
    return NULL;
}

// ───────────────────────────────────────────────────────────────
// Shutdown handler
// ───────────────────────────────────────────────────────────────
static void handle_sigint(int sig) {
    (void)sig;
    printf("\n[server] shutting down...\n");
    if (server_fd != -1) close(server_fd);
    store_shutdown();
    exit(0);
}

// ───────────────────────────────────────────────────────────────
// Password prompt
// ───────────────────────────────────────────────────────────────
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

// ───────────────────────────────────────────────────────────────
// Main
// ───────────────────────────────────────────────────────────────
int main() {
    signal(SIGINT, handle_sigint);

    char password[128];
    printf("Enter store password: ");
    fflush(stdout);
    read_password(password, sizeof(password));
    if (strlen(password) == 0) {
        fprintf(stderr, "Error: empty password\n");
        return 1;
    }

    store_init(password);
    memset(password, 0, sizeof(password));

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
