#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT     6379
#define BUF_SIZE 4096

// ─── Client library ───────────────────────────────────────────────────────────
typedef struct { int fd; } DbConn;

DbConn *db_connect(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd); return NULL;
    }

    DbConn *conn = malloc(sizeof(DbConn));
    conn->fd = fd;
    return conn;
}

void db_disconnect(DbConn *conn) {
    close(conn->fd);
    free(conn);
}

// Sends a command, reads one response line into out_buf
static int send_recv(DbConn *conn, const char *cmd, char *out_buf, size_t out_size) {
    write(conn->fd, cmd, strlen(cmd));

    ssize_t n = read(conn->fd, out_buf, out_size - 1);
    if (n <= 0) return 0;
    out_buf[n] = '\0';
    return 1;
}

int db_put(DbConn *conn, const char *key, const char *value) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "PUT %s %s\n", key, value);
    char response[256];
    send_recv(conn, cmd, response, sizeof(response));
    return strncmp(response, "OK", 2) == 0;
}

// Returns heap-allocated value string, or NULL if not found. Caller must free().
char *db_get(DbConn *conn, const char *key) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "GET %s\n", key);
    char response[4096];
    if (!send_recv(conn, cmd, response, sizeof(response))) return NULL;
    if (strncmp(response, "NIL", 3) == 0) return NULL;
    if (strncmp(response, "VALUE ", 6) == 0) {
        char *val = strdup(response + 6);
        val[strcspn(val, "\n")] = '\0';
        return val;
    }
    return NULL;
}

int db_delete(DbConn *conn, const char *key) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "DEL %s\n", key);
    char response[256];
    send_recv(conn, cmd, response, sizeof(response));
    return strncmp(response, "OK", 2) == 0;
}

void db_keys(DbConn *conn) {
    char response[4096];
    send_recv(conn, "KEYS\n", response, sizeof(response));
    printf("%s", response);
}

// ─── Interactive CLI ──────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";

    DbConn *conn = db_connect(host, PORT);
    if (!conn) {
        fprintf(stderr, "Error: could not connect to %s:%d\n", host, PORT);
        return 1;
    }
    printf("Connected to %s:%d\n", host, PORT);
    printf("Commands: put <key> <value> | get <key> | delete <key> | keys | ping | exit\n\n");

    char line[4096];
    char key[256], value[1024];

    printf("db> ");
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';

        if (sscanf(line, "put %255s %1023[^\n]", key, value) == 2) {
            printf(db_put(conn, key, value) ? "OK\n" : "ERROR\n");

        } else if (sscanf(line, "get %255s", key) == 1) {
            char *val = db_get(conn, key);
            printf(val ? "%s\n" : "(nil)\n", val);
            free(val);

        } else if (sscanf(line, "delete %255s", key) == 1) {
            printf(db_delete(conn, key) ? "OK\n" : "ERROR\n");

        } else if (strcmp(line, "keys") == 0) {
            db_keys(conn);

        } else if (strcmp(line, "ping") == 0) {
            char response[64];
            send_recv(conn, "PING\n", response, sizeof(response));
            printf("%s", response);

        } else if (strcmp(line, "exit") == 0) {
            break;

        } else if (strlen(line) > 0) {
            printf("Unknown command\n");
        }
        printf("db> ");
    }

    db_disconnect(conn);
    return 0;
}