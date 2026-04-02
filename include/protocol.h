#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

// ─── Command Types ────────────────────────────────────────────────────────────
typedef enum {
    CMD_PUT,
    CMD_GET,
    CMD_DEL,
    CMD_KEYS,
    CMD_PING,
    CMD_UNKNOWN
} CmdType;

// ─── Parsed Command ───────────────────────────────────────────────────────────
typedef struct {
    CmdType type;
    char    key[256];
    char    value[1024];
} Command;

// ─── Response Status ──────────────────────────────────────────────────────────
typedef enum {
    RESP_OK,
    RESP_VALUE,
    RESP_NIL,
    RESP_KEYS,
    RESP_PONG,
    RESP_ERROR
} RespType;

typedef struct {
    RespType type;
    char     body[4096];
} Response;

// ─── API ──────────────────────────────────────────────────────────────────────
int  protocol_parse_command(const char *line, Command *out);
void protocol_send_response(int fd, RespType type, const char *body);
int  protocol_parse_response(const char *raw, Response *out);

#endif