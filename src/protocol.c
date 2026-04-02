#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ─── Parse incoming command line into a Command struct ────────────────────────
int protocol_parse_command(const char *line, Command *out) {
    memset(out, 0, sizeof(Command));

    if (sscanf(line, "PUT %255s %1023[^\n]", out->key, out->value) == 2) {
        out->type = CMD_PUT;
        return 1;
    }
    if (sscanf(line, "GET %255s", out->key) == 1 &&
        strncmp(line, "GET", 3) == 0) {
        out->type = CMD_GET;
        return 1;
    }
    if (sscanf(line, "DEL %255s", out->key) == 1 &&
        strncmp(line, "DEL", 3) == 0) {
        out->type = CMD_DEL;
        return 1;
    }
    if (strncmp(line, "KEYS", 4) == 0) {
        out->type = CMD_KEYS;
        return 1;
    }
    if (strncmp(line, "PING", 4) == 0) {
        out->type = CMD_PING;
        return 1;
    }

    out->type = CMD_UNKNOWN;
    return 0;
}

// ─── Send a framed response to a file descriptor ─────────────────────────────
// Wire format:
//   OK\n
//   VALUE <body>\n
//   NIL\n
//   KEYS\n<body>\n
//   PONG\n
//   ERROR <body>\n
void protocol_send_response(int fd, RespType type, const char *body) {
    char buf[4096];

    switch (type) {
        case RESP_OK:
            write(fd, "OK\n", 3);
            break;
        case RESP_VALUE:
            snprintf(buf, sizeof(buf), "VALUE %s\n", body ? body : "");
            write(fd, buf, strlen(buf));
            break;
        case RESP_NIL:
            write(fd, "NIL\n", 4);
            break;
        case RESP_KEYS:
            snprintf(buf, sizeof(buf), "KEYS\n%s\n", body ? body : "");
            write(fd, buf, strlen(buf));
            break;
        case RESP_PONG:
            write(fd, "PONG\n", 5);
            break;
        case RESP_ERROR:
            snprintf(buf, sizeof(buf), "ERROR %s\n", body ? body : "unknown");
            write(fd, buf, strlen(buf));
            break;
    }
}

// ─── Parse a raw response string into a Response struct ──────────────────────
int protocol_parse_response(const char *raw, Response *out) {
    memset(out, 0, sizeof(Response));

    if (strncmp(raw, "OK", 2) == 0) {
        out->type = RESP_OK;
        return 1;
    }
    if (strncmp(raw, "VALUE ", 6) == 0) {
        out->type = RESP_VALUE;
        strncpy(out->body, raw + 6, sizeof(out->body) - 1);
        // strip trailing newline
        out->body[strcspn(out->body, "\n")] = '\0';
        return 1;
    }
    if (strncmp(raw, "NIL", 3) == 0) {
        out->type = RESP_NIL;
        return 1;
    }
    if (strncmp(raw, "KEYS", 4) == 0) {
        out->type = RESP_KEYS;
        // body starts after "KEYS\n"
        if (strlen(raw) > 5)
            strncpy(out->body, raw + 5, sizeof(out->body) - 1);
        return 1;
    }
    if (strncmp(raw, "PONG", 4) == 0) {
        out->type = RESP_PONG;
        return 1;
    }
    if (strncmp(raw, "ERROR ", 6) == 0) {
        out->type = RESP_ERROR;
        strncpy(out->body, raw + 6, sizeof(out->body) - 1);
        out->body[strcspn(out->body, "\n")] = '\0';
        return 1;
    }

    out->type = RESP_ERROR;
    snprintf(out->body, sizeof(out->body), "malformed response: %s", raw);
    return 0;
}