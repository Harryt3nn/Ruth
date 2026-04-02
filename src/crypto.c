#include "crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>

// ─── Curve25519 keypair ───────────────────────────────────────────────────────
int crypto_keypair(KeyPair *out) {
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    if (!ctx) return 0;

    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_keygen_init(ctx) <= 0 ||
        EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return 0;
    }

    size_t len = CURVE25519_KEY_LEN;
    EVP_PKEY_get_raw_public_key(pkey, out->pub, &len);
    len = CURVE25519_KEY_LEN;
    EVP_PKEY_get_raw_private_key(pkey, out->priv, &len);

    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    return 1;
}

// ─── ECDH + HKDF → session key ───────────────────────────────────────────────
int crypto_derive_session(
        const uint8_t *our_priv,
        const uint8_t *their_pub,
        SessionKey    *out)
{
    // Load our private key
    EVP_PKEY *priv_key = EVP_PKEY_new_raw_private_key(
            EVP_PKEY_X25519, NULL, our_priv, CURVE25519_KEY_LEN);
    // Load their public key
    EVP_PKEY *pub_key = EVP_PKEY_new_raw_public_key(
            EVP_PKEY_X25519, NULL, their_pub, CURVE25519_KEY_LEN);

    if (!priv_key || !pub_key) return 0;

    // ECDH
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(priv_key, NULL);
    uint8_t shared[CURVE25519_KEY_LEN];
    size_t  shared_len = sizeof(shared);

    if (EVP_PKEY_derive_init(ctx) <= 0 ||
        EVP_PKEY_derive_set_peer(ctx, pub_key) <= 0 ||
        EVP_PKEY_derive(ctx, shared, &shared_len) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(priv_key);
        EVP_PKEY_free(pub_key);
        return 0;
    }

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(priv_key);
    EVP_PKEY_free(pub_key);

    // HKDF: shared secret → 32-byte session key
    // Salt: fixed string so both sides derive the same key
    const uint8_t salt[] = "ruth-db-session-v1";
    const uint8_t info[] = "chacha20-poly1305";

    EVP_PKEY_CTX *kdf_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
    size_t key_len = CHACHA_KEY_LEN;

    if (EVP_PKEY_derive_init(kdf_ctx) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(kdf_ctx, EVP_sha256()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_salt(kdf_ctx, salt, sizeof(salt)-1) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(kdf_ctx, shared, shared_len) <= 0 ||
        EVP_PKEY_CTX_add1_hkdf_info(kdf_ctx, info, sizeof(info)-1) <= 0 ||
        EVP_PKEY_derive(kdf_ctx, out->key, &key_len) <= 0) {
        EVP_PKEY_CTX_free(kdf_ctx);
        return 0;
    }

    EVP_PKEY_CTX_free(kdf_ctx);

    // Clear shared secret from memory immediately
    memset(shared, 0, sizeof(shared));
    return 1;
}

// ─── ChaCha20-Poly1305 encrypt ────────────────────────────────────────────────
// Wire format: [12-byte nonce][ciphertext][16-byte Poly1305 tag]
int crypto_encrypt(
        const SessionKey *key,
        const uint8_t    *plaintext,
        size_t            len,
        uint8_t          *out_buf,
        size_t           *out_len)
{
    uint8_t nonce[CHACHA_NONCE_LEN];
    if (RAND_bytes(nonce, sizeof(nonce)) != 1) return 0;

    memcpy(out_buf, nonce, CHACHA_NONCE_LEN);

    int cipher_len = 0, final_len = 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    if (EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, key->key, nonce) <= 0 ||
        EVP_EncryptUpdate(ctx,
            out_buf + CHACHA_NONCE_LEN, &cipher_len,
            plaintext, (int)len) <= 0 ||
        EVP_EncryptFinal_ex(ctx,
            out_buf + CHACHA_NONCE_LEN + cipher_len, &final_len) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        return 0;
    }

    // Append Poly1305 tag
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, POLY1305_TAG_LEN,
        out_buf + CHACHA_NONCE_LEN + cipher_len + final_len);

    *out_len = CHACHA_NONCE_LEN + cipher_len + final_len + POLY1305_TAG_LEN;
    EVP_CIPHER_CTX_free(ctx);
    return 1;
}

// ─── ChaCha20-Poly1305 decrypt ────────────────────────────────────────────────
int crypto_decrypt(
        const SessionKey *key,
        const uint8_t    *in_buf,
        size_t            in_len,
        uint8_t          *plaintext,
        size_t           *out_len)
{
    if (in_len < CHACHA_NONCE_LEN + POLY1305_TAG_LEN) return 0;

    const uint8_t *nonce      = in_buf;
    const uint8_t *ciphertext = in_buf + CHACHA_NONCE_LEN;
    size_t         cipher_len = in_len - CHACHA_NONCE_LEN - POLY1305_TAG_LEN;
    const uint8_t *tag        = in_buf + CHACHA_NONCE_LEN + cipher_len;

    int plain_len = 0, final_len = 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    if (EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, key->key, nonce) <= 0 ||
        EVP_DecryptUpdate(ctx, plaintext, &plain_len, ciphertext, (int)cipher_len) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        return 0;
    }

    // Set tag before finalising — Poly1305 authentication happens here
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, POLY1305_TAG_LEN, (void*)tag);

    int ok = EVP_DecryptFinal_ex(ctx, plaintext + plain_len, &final_len);
    EVP_CIPHER_CTX_free(ctx);

    if (ok <= 0) return 0;  // tag mismatch — tampered or wrong key

    *out_len = plain_len + final_len;
    plaintext[*out_len] = '\0';
    return 1;
}

// ─── Hex helpers ──────────────────────────────────────────────────────────────
void crypto_to_hex(const uint8_t *in, size_t len, char *out_hex) {
    for (size_t i = 0; i < len; i++)
        sprintf(out_hex + i * 2, "%02x", in[i]);
    out_hex[len * 2] = '\0';
}

int crypto_from_hex(const char *hex, uint8_t *out, size_t out_len) {
    size_t hex_len = strlen(hex);
    if (hex_len != out_len * 2) return 0;
    for (size_t i = 0; i < out_len; i++)
        sscanf(hex + i * 2, "%02hhx", &out[i]);
    return 1;
}