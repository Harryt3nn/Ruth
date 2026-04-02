#include "session.h"
#include <string.h>

int session_init(Session *s) {
    memset(s, 0, sizeof(Session));
    if (!crypto_keypair(&s->keypair)) return 0;
    s->established_at = time(NULL);
    s->valid = 1;
    return 1;
}

int session_needs_rotation(const Session *s) {
    if (!s->valid) return 1;
    return (time(NULL) - s->established_at) >= SESSION_ROTATION_SECS;
}

void session_invalidate(Session *s) {
    // Zero out key material before invalidating
    memset(&s->key, 0, sizeof(SessionKey));
    memset(&s->keypair, 0, sizeof(KeyPair));
    s->valid = 0;
}