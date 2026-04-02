#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

typedef enum {
    CMD_PUT,
    CMD_GET,
    CMD_DEL,
    CMD_KEYS,
    CMD_PING,
    CMD_HELLO,    // ← new: initiates handshake, carries public key
    CMD_ROTATE,   // ← new: triggers key rotation
    CMD_UNKNOWN
} CmdType;

typedef struct {
    CmdType type;
    char    key[256];
    char    value[1024];
    char    pubkey_hex[65];  // 32 bytes as hex
} Command;

typedef enum {
    RESP_OK,
    RESP_VALUE,
    RESP_NIL,
    RESP_KEYS,
    RESP_PONG,
    RESP_HELLO,   // ← new: server responds with its public key
    RESP_ERROR
} RespType;

typedef struct {
    RespType type;
    char     body[4096];
    char     pubkey_hex[65];
} Response;

int  protocol_parse_command(const char *line, Command *out);
void protocol_send_response(int fd, RespType type, const char *body);
int  protocol_parse_response(const char *raw, Response *out);

#endif