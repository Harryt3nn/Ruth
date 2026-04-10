#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stddef.h>

typedef enum // command types (from client)
{
    CMD_PUT, // establish new value for given key
    CMD_GET, // fetch value connected to given key
    CMD_DEL, // remove value from said key
    CMD_KEYS, // returns list of all keys
    CMD_PING, // PONG
    CMD_HELLO, // initiates handshake, carries public key (secure session protocol)
    CMD_ROTATE, // triggers key rotation (secure session protocol)
    CMD_UNKNOWN // umberella for everything that isn't one of the other commands
} CmdType;

typedef struct 
{
    CmdType type; // which type was parsed into individual command
    char key[256]; // some commands need a key (e.g. CMD_GET)
    char value[1024]; // some commands need a value (CMD_PUT)
    char pubkey_hex[65];  // 32 bytes as hex (only used for CMD_HELLO) - Ruth uses Curve25519 public keys
} Command;

typedef enum // response from server
{
    RESP_OK, // succesful operation
    RESP_VALUE, // return value (CMD_GET)
    RESP_NIL, // no value to return (CMD_GET)
    RESP_KEYS, // list of keys (CMD_KEYS)
    RESP_PONG, // PING
    RESP_HELLO, // server responds with its public key
    RESP_ERROR // something unexpected happened
} RespType;

typedef struct 
{
    RespType type; // server response type (e.g. RESP_OK)
    char body[4096]; // payload value
    char pubkey_hex[65]; // (used in RESP_HELLO)
} Response;

int  protocol_parse_command(const char *line, Command *out); // tokenises raw text line, determines command type
void protocol_send_response(int fd, RespType type, const char *body); // response serialised into text
int  protocol_parse_response(const char *raw, Response *out); // raw text to response

#endif