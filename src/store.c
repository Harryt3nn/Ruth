#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include "khash.h"


// Wal stores the log; raft determines which entries are committed 
// store.c applies committed entries

// Constants 
#define KEY_LEN     32
#define NONCE_LEN   12
#define TAG_LEN     16
#define PBKDF2_ITER 200000

static const unsigned char SALT[16] = 
{
    0x4b,0x3a,0x1e,0x8f,0x22,0xd4,0x91,0x7c,
    0x5b,0x0e,0x3f,0xa6,0x88,0x12,0xcd,0x50
};

// Globals 
KHASH_MAP_INIT_STR(kvmap, char*)

static khash_t(kvmap) *h;
static FILE            *log_fp;
static const char      *LOG_PATH = "db.log";
static unsigned char    aes_key[KEY_LEN];
static pthread_mutex_t  store_lock = PTHREAD_MUTEX_INITIALIZER;

// Key Derivation
static void derive_key(const char *password) 
{
    if (PKCS5_PBKDF2_HMAC(password, strlen(password), SALT, sizeof(SALT), PBKDF2_ITER, EVP_sha256(), KEY_LEN, aes_key) != 1) 
    {
        fprintf(stderr, "Error: key derivation failed\n");
        exit(1);
    }
}

// Encryption 
static int encrypt_value(const char *plaintext, char *out_hex, size_t out_size)
 {
    unsigned char nonce[NONCE_LEN], tag[TAG_LEN];
    int pt_len = strlen(plaintext);
    unsigned char *cipher = malloc(pt_len + TAG_LEN);
    if (!cipher) 
    {
        return 0;
    }
    if (RAND_bytes(nonce, NONCE_LEN) != 1) 
    { 
        free(cipher);
        return 0; 
    }

    int len, cipher_len;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_LEN, NULL);
    EVP_EncryptInit_ex(ctx, NULL, NULL, aes_key, nonce);
    EVP_EncryptUpdate(ctx, cipher, &len, (unsigned char*)plaintext, pt_len);
    cipher_len = len;
    EVP_EncryptFinal_ex(ctx, cipher + len, &len);
    cipher_len += len;
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_LEN, tag);
    EVP_CIPHER_CTX_free(ctx);

    size_t needed = (NONCE_LEN + TAG_LEN + cipher_len) * 2 + 1;
    if (out_size < needed) 
    { 
        free(cipher); 
        return 0; 
    }

    int pos = 0;
    for (int i = 0; i < NONCE_LEN;  i++) pos += sprintf(out_hex + pos, "%02x", nonce[i]);
    for (int i = 0; i < TAG_LEN;    i++) pos += sprintf(out_hex + pos, "%02x", tag[i]);
    for (int i = 0; i < cipher_len; i++) pos += sprintf(out_hex + pos, "%02x", cipher[i]);

    free(cipher);
    return 1;
}

static int decrypt_value(const char *in_hex, char *plaintext, size_t pt_size) 
{
    int hex_len = strlen(in_hex);
    if (hex_len < (NONCE_LEN + TAG_LEN) * 2) 
    {
        return 0;
    }
    int total = hex_len / 2;
    unsigned char *raw = malloc(total);
    if (!raw) 
    {
         return 0;
    }
    
    for (int i = 0; i < total; i++)
        sscanf(in_hex + i * 2, "%02hhx", &raw[i]);

    unsigned char *nonce = raw;
    unsigned char *tag = raw + NONCE_LEN;
    unsigned char *cipher = raw + NONCE_LEN + TAG_LEN;
    int cipher_len = total - NONCE_LEN - TAG_LEN;

    if ((size_t)cipher_len + 1 > pt_size) 
    { 
        free(raw); 
        return 0; 
    }

    int len;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_LEN, NULL);
    EVP_DecryptInit_ex(ctx, NULL, NULL, aes_key, nonce);
    EVP_DecryptUpdate(ctx, (unsigned char*)plaintext, &len, cipher, cipher_len);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_LEN, tag);

    int ok = EVP_DecryptFinal_ex(ctx, (unsigned char*)plaintext + len, &len);
    EVP_CIPHER_CTX_free(ctx);
    free(raw);

    if (ok <= 0) return 0;
    plaintext[cipher_len] = '\0';
    return 1;
}

// Internal hash table ops (call with lock held)
static void _put(const char *key, const char *value) 
{
    int ret;
    khint_t k = kh_put(kvmap, h, key, &ret);
    if (ret == -1) return;
    if (ret == 0) 
    {
        free((char*)kh_val(h, k));
    } 
    else 
    {
        kh_key(h, k) = strdup(key);
    }
    kh_val(h, k) = strdup(value);
}

static void _delete(const char *key) 
{
    khint_t k = kh_get(kvmap, h, key);
    if (k != kh_end(h)) 
    {
        free((char*)kh_key(h, k));
        free((char*)kh_val(h, k));
        kh_del(kvmap, h, k);
    }
}

// Log 
static void log_open() 
{
    log_fp = fopen(LOG_PATH, "a");
    if (!log_fp) 
    { 
        fprintf(stderr, "Error: cannot open log\n"); exit(1); 
    }
}

static void log_replay() 
{
    FILE *f = fopen(LOG_PATH, "r");
    if (!f) return;

    char op[8], key[256], hex[2048], plaintext[1024];
    while (fscanf(f, "%7s", op) == 1) 
    {
        if (strcmp(op, "PUT") == 0) 
        {
            if (fscanf(f, "%255s %2047s", key, hex) == 2) 
            {
                if (!decrypt_value(hex, plaintext, sizeof(plaintext))) 
                {
                    fprintf(stderr, "Error: decryption failed — wrong password?\n");
                    fclose(f);
                    exit(1);
                }
                _put(key, plaintext);
            }
        } 
        else if (strcmp(op, "DEL") == 0) 
        {
            if (fscanf(f, "%255s", key) == 1) _delete(key);
        } 
        else 
        {
            fscanf(f, "%*[^\n]\n");
        }
    }
    fclose(f);
}

static void log_write_put(const char *key, const char *value)
 {
    char hex[2048];
    if (!encrypt_value(value, hex, sizeof(hex))) return;
    fprintf(log_fp, "PUT %s %s\n", key, hex);
    fflush(log_fp);
}

static void log_write_del(const char *key) 
{
    fprintf(log_fp, "DEL %s\n", key);
    fflush(log_fp);
}

// Public API functions:
void store_init(const char *password) 
{
    derive_key(password);
    h = kh_init(kvmap);
    log_replay();
    log_open();
}

void store_shutdown() 
{
    khint_t k;
    for (k = kh_begin(h); k != kh_end(h); ++k) 
    {
        if (!kh_exist(h, k)) continue;
        free((char*)kh_key(h, k));
        free((char*)kh_val(h, k));
    }
    kh_destroy(kvmap, h);
    if (log_fp) fclose(log_fp);
    pthread_mutex_destroy(&store_lock);
}

void store_put(const char *key, const char *value) 
{
    pthread_mutex_lock(&store_lock);
    _put(key, value);
    log_write_put(key, value);
    pthread_mutex_unlock(&store_lock);
}

const char *store_get(const char *key) 
{
    pthread_mutex_lock(&store_lock);
    khint_t k = kh_get(kvmap, h, key);
    const char *val = (k == kh_end(h)) ? NULL : kh_val(h, k);
    // NOTE: caller must use value before next store call
    pthread_mutex_unlock(&store_lock);
    return val;
}

void store_delete(const char *key) 
{
    pthread_mutex_lock(&store_lock);
    _delete(key);
    log_write_del(key);
    pthread_mutex_unlock(&store_lock);
}

// Returns heap-allocated string of all keys, newline separated.
// Caller must free().
char *store_keys()
 {
    pthread_mutex_lock(&store_lock);
    size_t total = 0;
    khint_t k;
    for (k = kh_begin(h); k != kh_end(h); ++k) 
    {
        if (!kh_exist(h, k)) continue;
        total += strlen(kh_key(h, k)) + 1;
    }

    if (total == 0) 
    {
        pthread_mutex_unlock(&store_lock);
        return strdup("(empty)");
    }

    char *buf = malloc(total + 1);
    buf[0] = '\0';
    for (k = kh_begin(h); k != kh_end(h); ++k) 
    {
        if (!kh_exist(h, k)) continue;
        strcat(buf, kh_key(h, k));
        strcat(buf, "\n");
    }
    pthread_mutex_unlock(&store_lock);
    return buf;
}