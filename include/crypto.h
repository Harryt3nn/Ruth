#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#define CURVE25519_KEY_LEN  32
#define CHACHA_KEY_LEN      32
#define CHACHA_NONCE_LEN    12
#define POLY1305_TAG_LEN    16
#define HKDF_SALT_LEN       32

// ─── Curve25519 keypair ───────────────────────────────────────────────────────
typedef struct {
    uint8_t pub[CURVE25519_KEY_LEN];
    uint8_t priv[CURVE25519_KEY_LEN];
} KeyPair;

// ─── Session key (derived from shared secret) ─────────────────────────────────
typedef struct {
    uint8_t key[CHACHA_KEY_LEN];
} SessionKey;

// ─── API ──────────────────────────────────────────────────────────────────────

// Generate a fresh Curve25519 keypair
int  crypto_keypair(KeyPair *out);

// Perform ECDH: combine our private key + their public key → shared secret
// Then run HKDF to derive a session key
int  crypto_derive_session(
        const uint8_t *our_priv,
        const uint8_t *their_pub,
        SessionKey    *out);

// Encrypt plaintext with ChaCha20-Poly1305
// out_buf must be at least: CHACHA_NONCE_LEN + len + POLY1305_TAG_LEN
int  crypto_encrypt(
        const SessionKey *key,
        const uint8_t    *plaintext,
        size_t            len,
        uint8_t          *out_buf,
        size_t           *out_len);

// Decrypt — returns 1 on success, 0 if authentication fails (tampered)
int  crypto_decrypt(
        const SessionKey *key,
        const uint8_t    *in_buf,
        size_t            in_len,
        uint8_t          *plaintext,
        size_t           *out_len);

// Hex encode/decode helpers
void    crypto_to_hex(const uint8_t *in, size_t len, char *out_hex);
int     crypto_from_hex(const char *hex, uint8_t *out, size_t out_len);

#endif