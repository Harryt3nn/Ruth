#include "client.h"
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUF_SIZE 4096

// ─── Internal: send command, receive raw response ─────────────────────────────
static int send_recv(DbConn *conn, const char *cmd, char *out_buf, size_t out_size) {
    if (write(conn->fd, cmd, strlen(cmd)) < 0) return 0;

    ssize_t n = read(conn->fd, out_buf, out_size - 1);
    if (n <= 0) return 0;
    out_buf[n] = '\0';
    return 1;
}

// ─── Connection ───────────────────────────────────────────────────────────────
DbConn *db_connect(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        close(fd); return NULL;
    }
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd); return NULL;
    }

    DbConn *conn = malloc(sizeof(DbConn));
    conn->fd   = fd;
    conn->port = port;
    strncpy(conn->host, host, sizeof(conn->host) - 1);
    return conn;
}

void db_disconnect(DbConn *conn) {
    if (!conn) return;
    close(conn->fd);
    free(conn);
}

int db_ping(DbConn *conn) {
    char response[BUF_SIZE];
    if (!send_recv(conn, "PING\n", response, sizeof(response))) return 0;
    Response r;
    protocol_parse_response(response, &r);
    return r.type == RESP_PONG;
}

// ─── Core operations ──────────────────────────────────────────────────────────
int db_put(DbConn *conn, const char *key, const char *value) {
    char cmd[BUF_SIZE];
    snprintf(cmd, sizeof(cmd), "PUT %s %s\n", key, value);
    char response[BUF_SIZE];
    if (!send_recv(conn, cmd, response, sizeof(response))) return 0;
    Response r;
    protocol_parse_response(response, &r);
    return r.type == RESP_OK;
}

char *db_get(DbConn *conn, const char *key) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "GET %s\n", key);
    char response[BUF_SIZE];
    if (!send_recv(conn, cmd, response, sizeof(response))) return NULL;
    Response r;
    protocol_parse_response(response, &r);
    if (r.type != RESP_VALUE) return NULL;
    return strdup(r.body);
}

int db_delete(DbConn *conn, const char *key) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "DEL %s\n", key);
    char response[BUF_SIZE];
    if (!send_recv(conn, cmd, response, sizeof(response))) return 0;
    Response r;
    protocol_parse_response(response, &r);
    return r.type == RESP_OK;
}

char *db_keys(DbConn *conn) {
    char response[BUF_SIZE];
    if (!send_recv(conn, "KEYS\n", response, sizeof(response))) return NULL;
    Response r;
    protocol_parse_response(response, &r);
    if (r.type != RESP_KEYS) return NULL;
    return strdup(r.body);
}