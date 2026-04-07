#ifndef CLIENT_H
#define CLIENT_H
#include "session.h"

typedef struct 
{
    int     fd;
    char    host[64];
    int     port;
    Session session;   // ← holds Curve25519 keypair + ChaCha20 session key
} DbConn;

DbConn *db_connect(const char *host, int port);
void    db_disconnect(DbConn *conn);
int     db_ping(DbConn *conn);
int     db_put(DbConn *conn, const char *key, const char *value);
char   *db_get(DbConn *conn, const char *key);  // caller must free()
int     db_delete(DbConn *conn, const char *key);
char   *db_keys(DbConn *conn);                  // caller must free()

#endif