#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>


// protocol determines what commands a client can send and responses a server can run
// in charge of handshake messages used to establish sessions

int protocol_parse_command(const char *line, Command *out) 
{
    memset(out, 0, sizeof(Command));

    if (sscanf(line, "PUT %255s %1023[^\n]", out->key, out->value) == 2 && strncmp(line, "PUT", 3) == 0) 
    {
        out->type = CMD_PUT; return 1;
    }
    if (sscanf(line, "GET %255s", out->key) == 1 && strncmp(line, "GET", 3) == 0) 
    {
        out->type = CMD_GET; return 1;
    }
    if (sscanf(line, "DEL %255s", out->key) == 1 && strncmp(line, "DEL", 3) == 0) 
    {
        out->type = CMD_DEL; return 1;
    }
    if (strncmp(line, "KEYS", 4) == 0) 
    {
        out->type = CMD_KEYS; return 1;
    }
    if (strncmp(line, "PING", 4) == 0) 
    {
        out->type = CMD_PING; return 1;
    }
    // HELLO <pubkey_hex>
    if (sscanf(line, "HELLO %64s", out->pubkey_hex) == 1 && strncmp(line, "HELLO", 5) == 0)
    {
        out->type = CMD_HELLO; return 1;
    }
    if (strncmp(line, "ROTATE", 6) == 0) 
    {
        out->type = CMD_ROTATE; return 1;
    }
    out->type = CMD_UNKNOWN;
    return 0;
}

void protocol_send_response(int fd, RespType type, const char *body) 
{
    char buf[4096];
    switch (type) 
    {
        case RESP_OK:
            write(fd, "OK\n", 3); break;
        case RESP_VALUE:
            snprintf(buf, sizeof(buf), "VALUE %s\n", body ? body : "");
            write(fd, buf, strlen(buf)); break;
        case RESP_NIL:
            write(fd, "NIL\n", 4); break;
        case RESP_KEYS:
            snprintf(buf, sizeof(buf), "KEYS\n%s\n", body ? body : "");
            write(fd, buf, strlen(buf)); break;
        case RESP_PONG:
            write(fd, "PONG\n", 5); break;
        case RESP_HELLO:
            // HELLO <pubkey_hex>
            snprintf(buf, sizeof(buf), "HELLO %s\n", body ? body : "");
            write(fd, buf, strlen(buf)); break;
        case RESP_ERROR:
            snprintf(buf, sizeof(buf), "ERROR %s\n", body ? body : "unknown");
            write(fd, buf, strlen(buf)); break;
    }
}

int protocol_parse_response(const char *raw, Response *out) 
{
    memset(out, 0, sizeof(Response));
    if (strncmp(raw, "OK", 2) == 0) 
    { 
        out->type = RESP_OK;   return 1; 
    }
    if (strncmp(raw, "NIL", 3) == 0) 
    { 
        out->type = RESP_NIL;  return 1; 
    }
    if (strncmp(raw, "PONG", 4) == 0) 
    { 
        out->type = RESP_PONG; return 1; 
    }
    if (strncmp(raw, "VALUE ", 6) == 0) 
    {
        out->type = RESP_VALUE;
        strncpy(out->body, raw + 6, sizeof(out->body) - 1);
        out->body[strcspn(out->body, "\n")] = '\0';
        return 1;
    }
    if (strncmp(raw, "KEYS\n", 5) == 0) 
    {
        out->type = RESP_KEYS;
        strncpy(out->body, raw + 5, sizeof(out->body) - 1);
        return 1;
    }
    if (strncmp(raw, "HELLO ", 6) == 0) 
    {
        out->type = RESP_HELLO;
        strncpy(out->pubkey_hex, raw + 6, sizeof(out->pubkey_hex) - 1);
        out->pubkey_hex[strcspn(out->pubkey_hex, "\n")] = '\0';
        return 1;
    }
    if (strncmp(raw, "ERROR ", 6) == 0) 
    {
        out->type = RESP_ERROR;
        strncpy(out->body, raw + 6, sizeof(out->body) - 1);
        out->body[strcspn(out->body, "\n")] = '\0';
        return 1;
    }
    out->type = RESP_ERROR;
    snprintf(out->body, sizeof(out->body), "malformed: %s", raw);
    return 0;
}