#ifndef CLIENT_H
#define CLIENT_H

// ─── Connection handle ────────────────────────────────────────────────────────
typedef struct {
    int fd;
    char host[64];
    int  port;
} DbConn;

// ─── Connection ───────────────────────────────────────────────────────────────
DbConn *db_connect(const char *host, int port);
void    db_disconnect(DbConn *conn);
int     db_ping(DbConn *conn);               // returns 1 if server alive

// ─── Core operations ──────────────────────────────────────────────────────────
int   db_put(DbConn *conn, const char *key, const char *value);
char *db_get(DbConn *conn, const char *key); // caller must free()
int   db_delete(DbConn *conn, const char *key);
char *db_keys(DbConn *conn);                 // caller must free()

#endif