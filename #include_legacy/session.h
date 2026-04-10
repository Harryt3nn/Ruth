#ifndef SESSION_H
#define SESSION_H
#include "crypto.h"
#include <time.h>

#define SESSION_ROTATION_SECS (24 * 60 * 60)  // session lasts for 24 hours

typedef struct 
{
    SessionKey key; // temporary key for symmetric encryption of a single communication session. Used for encrypting
                    // messages after handshake

    KeyPair    keypair; // consists of public + private key, used for asymmetric operations
                        // asymmetric keypair used to establish the session

    time_t     established_at; // marks the time that the session was created
    int        valid; // validity flag
} Session;

int  session_init(Session *s); // fresh session, new key material
int  session_needs_rotation(const Session *s); // checks if session is too old and needs replacing
void session_invalidate(Session *s); // wipes session

#endif