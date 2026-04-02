#ifndef SESSION_H
#define SESSION_H

#include "crypto.h"
#include <time.h>

#define SESSION_ROTATION_SECS (24 * 60 * 60)  // 24 hours

typedef struct {
    SessionKey key;
    KeyPair    keypair;
    time_t     established_at;
    int        valid;
} Session;

int  session_init(Session *s);
int  session_needs_rotation(const Session *s);
void session_invalidate(Session *s);

#endif