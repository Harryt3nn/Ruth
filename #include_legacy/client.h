#ifndef CLIENT_H
#define CLIENT_H
#include "session.h"

typedef struct // live RuthDB connection
{
    int fd; // file descriptor for network connection
    char host[64]; // node connection
    int port; // node connection
    Session session; // holds Curve25519 keypair + ChaCha20 session key
} DbConn;


// API functions:

DbConn *db_connect(const char *host, int port); 
// opens TCP connection, initialising new session
// CMD_HELLO sent to establish handshake - receives public key from the server
// derives shared session key

void db_disconnect(DbConn *conn); // closes socket & zeroises session
int db_ping(DbConn *conn); // PONG

// server commands, await an OK
int db_put(DbConn *conn, const char *key, const char *value); 
char *db_get(DbConn *conn, const char *key); // caller must free()
int db_delete(DbConn *conn, const char *key);
char *db_keys(DbConn *conn); // caller must free()

#endif