#include "client.h"
#include "protocol.h"
#include "crypto.h"
#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define BUF_SIZE 4096

// provides persistent TCP server connection 
// establishes secure session + session rotation

// ─── Internal send/recv ───────────────────────────────────────────────────────
static int send_recv(DbConn *conn, const char *cmd,
                     char *out_buf, size_t out_size) {
    if (write(conn->fd, cmd, strlen(cmd)) < 0) return 0;
    ssize_t n = read(conn->fd, out_buf, out_size - 1);
    if (n <= 0) return 0;
    out_buf[n] = '\0';
    return 1;
}

// ─── Handshake: HELLO exchange + session key derivation ──────────────────────
static int do_handshake(DbConn *conn) {
    if (!session_init(&conn->session)) return 0;

    // Send our public key
    char pubkey_hex[65];
    crypto_to_hex(conn->session.keypair.pub, CURVE25519_KEY_LEN, pubkey_hex);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "HELLO %s\n", pubkey_hex);

    char response[BUF_SIZE];
    if (!send_recv(conn, cmd, response, sizeof(response))) return 0;

    // Parse server's public key from HELLO response
    Response r;
    protocol_parse_response(response, &r);
    if (r.type != RESP_HELLO) return 0;

    uint8_t server_pub[CURVE25519_KEY_LEN];
    if (!crypto_from_hex(r.pubkey_hex, server_pub, CURVE25519_KEY_LEN)) return 0;

    // Derive shared session key
    return crypto_derive_session(
        conn->session.keypair.priv,
        server_pub,
        &conn->session.key);
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

    DbConn *conn = calloc(1, sizeof(DbConn));
    conn->fd   = fd;
    conn->port = port;
    strncpy(conn->host, host, sizeof(conn->host) - 1);

    // Perform handshake immediately on connect
    if (!do_handshake(conn)) {
        fprintf(stderr, "Error: handshake failed\n");
        free(conn);
        close(fd);
        return NULL;
    }

    return conn;
}

void db_disconnect(DbConn *conn) {
    if (!conn) return;
    session_invalidate(&conn->session);
    close(conn->fd);
    free(conn);
}

// ─── Key rotation ─────────────────────────────────────────────────────────────
static int maybe_rotate(DbConn *conn) {
    if (!session_needs_rotation(&conn->session)) return 1;

    printf("[client] rotating session key...\n");

    // Tell server we're rotating
    char response[BUF_SIZE];
    if (!send_recv(conn, "ROTATE\n", response, sizeof(response))) return 0;

    // Server acknowledges with OK, then we do a fresh handshake
    Response r;
    protocol_parse_response(response, &r);
    if (r.type != RESP_OK) return 0;

    session_invalidate(&conn->session);
    return do_handshake(conn);
}

// ─── Encrypt value → hex, decrypt hex → value ────────────────────────────────
static int encrypt_to_hex(DbConn *conn, const char *value,
                           char *out_hex, size_t hex_size) {
    uint8_t cipher[2048];
    size_t  cipher_len;
    if (!crypto_encrypt(&conn->session.key,
                        (uint8_t*)value, strlen(value),
                        cipher, &cipher_len)) return 0;
    if (cipher_len * 2 + 1 > hex_size) return 0;
    crypto_to_hex(cipher, cipher_len, out_hex);
    return 1;
}

static char *decrypt_from_hex(DbConn *conn, const char *hex) {
    size_t  hex_len = strlen(hex);
    uint8_t *raw    = malloc(hex_len / 2);
    if (!raw) return NULL;
    if (!crypto_from_hex(hex, raw, hex_len / 2)) { free(raw); return NULL; }

    uint8_t plaintext[2048];
    size_t  plain_len;
    if (!crypto_decrypt(&conn->session.key,
                        raw, hex_len / 2,
                        plaintext, &plain_len)) {
        free(raw);
        return NULL;
    }
    free(raw);
    return strdup((char*)plaintext);
}

// ─── Core operations ──────────────────────────────────────────────────────────
int db_ping(DbConn *conn) {
    char response[BUF_SIZE];
    if (!send_recv(conn, "PING\n", response, sizeof(response))) return 0;
    Response r;
    protocol_parse_response(response, &r);
    return r.type == RESP_PONG;
}

int db_put(DbConn *conn, const char *key, const char *value) {
    if (!maybe_rotate(conn)) return 0;

    char hex[4096];
    if (!encrypt_to_hex(conn, value, hex, sizeof(hex)))
        return 0;

    char cmd[BUF_SIZE];
    snprintf(cmd, sizeof(cmd), "PUT %s %s\n", key, hex);

    char response[BUF_SIZE];
    if (!send_recv(conn, cmd, response, sizeof(response)))
        return 0;

    Response r;
    protocol_parse_response(response, &r);

    // NEW: print helpful hint if server returns an error
    if (r.type == RESP_ERROR) {
        printf("hint: %s\n", r.body);
        return 0;
    }

    return r.type == RESP_OK;
}

char *db_get(DbConn *conn, const char *key) {
    if (!maybe_rotate(conn)) return NULL;

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "GET %s\n", key);
    char response[BUF_SIZE];
    if (!send_recv(conn, cmd, response, sizeof(response))) return NULL;

    Response r;
    protocol_parse_response(response, &r);
    if (r.type != RESP_VALUE) return NULL;

    // Decrypt the ciphertext the server returned
    return decrypt_from_hex(conn, r.body);
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