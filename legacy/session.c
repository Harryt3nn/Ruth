#include "session.h"
#include <string.h>

int session_init(Session *s) // initialise the session
{
    memset(s, 0, sizeof(Session)); // struct is wiped to ensure no memory from previous sessions
    if (!crypto_keypair(&s->keypair)) return 0; // ephemeral keypair failure returns 0
    s->established_at = time(NULL); // record the creation time from the instance this line is run
    s->valid = 1; // mark session as active
    return 1;
}

int session_needs_rotation(const Session *s) // rotation needed if session is invalid (long lived keys are dangerous)
{
    if (!s->valid) return 1; // if already defined as invalid, session rotation needed
    return (time(NULL) - s->established_at) >= SESSION_ROTATION_SECS; // sessions should be rotated after 24hrs 
}

void session_invalidate(Session *s) // sensitive material wiped before session marked as invalid
{
    // keys shouldn't maintain in memory after use
    // Zero out key material before invalidating, 'zeroisation'
    memset(&s->key, 0, sizeof(SessionKey));
    memset(&s->keypair, 0, sizeof(KeyPair));
    s->valid = 0;
}