#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>         // STDIN_FILENO
#include <termios.h>        // terminal echo control

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "khash.h"

// ─── Constants ────────────────────────────────────────────────────────────────
#define KEY_LEN   32   // AES-256
#define NONCE_LEN 12   // GCM standard
#define TAG_LEN   16   // GCM auth tag
#define PBKDF2_ITER 200000

// Fixed salt — in a real system store this in the db header
// Generated once: openssl rand -hex 16
static const unsigned char SALT[16] = {
    0x4b,0x3a,0x1e,0x8f,0x22,0xd4,0x91,0x7c,
    0x5b,0x0e,0x3f,0xa6,0x88,0x12,0xcd,0x50
};

// ─── Hash Map ─────────────────────────────────────────────────────────────────
KHASH_MAP_INIT_STR(kvmap, char*)

static khash_t(kvmap) *h;
static FILE *log_fp;
static const char *LOG_PATH = "db.log";
static unsigned char aes_key[KEY_LEN];

// ─── Key Derivation ───────────────────────────────────────────────────────────

// Derives a 256-bit AES key from a password using PBKDF2-SHA256.
// The key is stored in the global aes_key[] buffer.
void derive_key(const char *password) {
    if (PKCS5_PBKDF2_HMAC(
            password, strlen(password),
            SALT, sizeof(SALT),
            PBKDF2_ITER,
            EVP_sha256(),
            KEY_LEN, aes_key) != 1) {
        fprintf(stderr, "Error: key derivation failed\n");
        exit(1);
    }
}

// Reads a password from stdin without echoing it to the terminal.
void read_password(char *buf, int maxlen) {
    struct termios old, noecho;
    tcgetattr(STDIN_FILENO, &old);
    noecho = old;
    noecho.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &noecho);

    fgets(buf, maxlen, stdin);
    buf[strcspn(buf, "\n")] = '\0';

    tcsetattr(STDIN_FILENO, TCSANOW, &old);  // restore echo
    printf("\n");
}

// ─── Encryption / Decryption ──────────────────────────────────────────────────

// Encrypts plaintext using AES-256-GCM.
// Output format (hex encoded): [12 byte nonce][16 byte tag][ciphertext]
// Returns 1 on success, 0 on failure.
int encrypt_value(const char *plaintext, char *out_hex, size_t out_hex_size) {
    unsigned char nonce[NONCE_LEN];
    unsigned char tag[TAG_LEN];
    int pt_len = strlen(plaintext);
    unsigned char *cipher = malloc(pt_len);
    if (!cipher) return 0;

    // Random nonce — never reuse with the same key
    if (RAND_bytes(nonce, NONCE_LEN) != 1) { free(cipher); return 0; }

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

    // Check output buffer is large enough
    // Total bytes: NONCE_LEN + TAG_LEN + cipher_len, each byte = 2 hex chars + null
    size_t needed = (NONCE_LEN + TAG_LEN + cipher_len) * 2 + 1;
    if (out_hex_size < needed) { free(cipher); return 0; }

    // Pack nonce + tag + ciphertext into hex string
    int pos = 0;
    for (int i = 0; i < NONCE_LEN;   i++) pos += sprintf(out_hex + pos, "%02x", nonce[i]);
    for (int i = 0; i < TAG_LEN;     i++) pos += sprintf(out_hex + pos, "%02x", tag[i]);
    for (int i = 0; i < cipher_len;  i++) pos += sprintf(out_hex + pos, "%02x", cipher[i]);

    free(cipher);
    return 1;
}

// Decrypts a hex-encoded nonce+tag+ciphertext string.
// Returns 1 on success, 0 if decryption or authentication fails.
// A failure here means either the wrong password or corrupted/tampered data.
int decrypt_value(const char *in_hex, char *plaintext, size_t pt_size) {
    int hex_len = strlen(in_hex);
    if (hex_len < (NONCE_LEN + TAG_LEN) * 2) return 0;

    int total_bytes = hex_len / 2;
    unsigned char *raw = malloc(total_bytes);
    if (!raw) return 0;

    for (int i = 0; i < total_bytes; i++)
        sscanf(in_hex + i * 2, "%02hhx", &raw[i]);

    unsigned char *nonce  = raw;
    unsigned char *tag    = raw + NONCE_LEN;
    unsigned char *cipher = raw + NONCE_LEN + TAG_LEN;
    int cipher_len        = total_bytes - NONCE_LEN - TAG_LEN;

    if ((size_t)cipher_len + 1 > pt_size) { free(raw); return 0; }

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

    if (ok <= 0) return 0;  // wrong password or tampered data
    plaintext[cipher_len] = '\0';
    return 1;
}

// ─── Hash Table Operations ────────────────────────────────────────────────────
void db_put(const char *key, const char *value) {
    int ret;
    khint_t k = kh_put(kvmap, h, key, &ret);
    if (ret == -1) {
        fprintf(stderr, "Error: failed to insert key '%s'\n", key);
        return;
    }
    if (ret == 0) {
        free((char*)kh_val(h, k));
    } else {
        kh_key(h, k) = strdup(key);
    }
    kh_val(h, k) = strdup(value);
}

const char *db_get(const char *key) {
    khint_t k = kh_get(kvmap, h, key);
    if (k == kh_end(h)) return NULL;
    return kh_val(h, k);
}

void db_delete(const char *key) {
    khint_t k = kh_get(kvmap, h, key);
    if (k != kh_end(h)) {
        free((char*)kh_key(h, k));
        free((char*)kh_val(h, k));
        kh_del(kvmap, h, k);
    }
}

void db_free_all() {
    khint_t k;
    for (k = kh_begin(h); k != kh_end(h); ++k) {
        if (!kh_exist(h, k)) continue;
        free((char*)kh_key(h, k));
        free((char*)kh_val(h, k));
    }
    kh_destroy(kvmap, h);
}

// ─── Log Operations ───────────────────────────────────────────────────────────
void log_open() {
    log_fp = fopen(LOG_PATH, "a");
    if (!log_fp) {
        fprintf(stderr, "Error: could not open log file '%s'\n", LOG_PATH);
        exit(1);
    }
}

// Encrypts value then writes: PUT <key> <hex_encrypted_value>
void log_write_put(const char *key, const char *value) {
    char hex[2048];
    if (!encrypt_value(value, hex, sizeof(hex))) {
        fprintf(stderr, "Error: encryption failed for key '%s'\n", key);
        return;
    }
    fprintf(log_fp, "PUT %s %s\n", key, hex);
    fflush(log_fp);
}

void log_write_del(const char *key) {
    fprintf(log_fp, "DEL %s\n", key);
    fflush(log_fp);
}

// Replay log — decrypt each value as it's read back
void log_replay() {
    FILE *f = fopen(LOG_PATH, "r");
    if (!f) return;

    char op[8], key[256], hex[2048], plaintext[1024];

    while (fscanf(f, "%7s", op) == 1) {
        if (strcmp(op, "PUT") == 0) {
            if (fscanf(f, "%255s %2047s", key, hex) == 2) {
                if (!decrypt_value(hex, plaintext, sizeof(plaintext))) {
                    fprintf(stderr, "Error: decryption failed for key '%s'.\n"
                                    "       Wrong password or corrupted log.\n", key);
                    fclose(f);
                    db_free_all();
                    exit(1);
                }
                db_put(key, plaintext);
            }
        } else if (strcmp(op, "DEL") == 0) {
            if (fscanf(f, "%255s", key) == 1) {
                db_delete(key);
            }
        } else {
            fscanf(f, "%*[^\n]\n");
        }
    }
    fclose(f);
}

void log_compact() {
    FILE *f = fopen("db.log.tmp", "w");
    if (!f) {
        fprintf(stderr, "Error: could not open temp file for compaction\n");
        return;
    }

    char hex[2048];
    khint_t k;
    for (k = kh_begin(h); k != kh_end(h); ++k) {
        if (!kh_exist(h, k)) continue;
        if (!encrypt_value(kh_val(h, k), hex, sizeof(hex))) {
            fprintf(stderr, "Error: encryption failed during compaction\n");
            fclose(f);
            return;
        }
        fprintf(f, "PUT %s %s\n", kh_key(h, k), hex);
    }
    fclose(f);

    if (log_fp) { fclose(log_fp); log_fp = NULL; }

    remove(LOG_PATH);
    if (rename("db.log.tmp", LOG_PATH) != 0) {
        fprintf(stderr, "Error: compaction rename failed\n");
    } else {
        printf("Compaction done.\n");
    }

    log_open();
}

// ─── CLI ──────────────────────────────────────────────────────────────────────
void print_help() {
    printf("Commands:\n");
    printf("  put <key> <value>  Store a key-value pair\n");
    printf("  get <key>          Retrieve a value\n");
    printf("  delete <key>       Delete a key\n");
    printf("  compact            Compact the log file\n");
    printf("  keys               List all keys\n");
    printf("  help               Show this message\n");
    printf("  exit               Quit\n");
}

void cmd_keys() {
    int count = 0;
    khint_t k;
    for (k = kh_begin(h); k != kh_end(h); ++k) {
        if (!kh_exist(h, k)) continue;
        printf("  %s\n", kh_key(h, k));
        count++;
    }
    if (count == 0) printf("  (empty)\n");
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main() {
    // Derive key from password before touching any data
    char password[128];
    printf("Enter password: ");
    fflush(stdout);
    read_password(password, sizeof(password));

    if (strlen(password) == 0) {
        fprintf(stderr, "Error: password cannot be empty\n");
        return 1;
    }

    derive_key(password);
    memset(password, 0, sizeof(password));  // clear password from memory

    h = kh_init(kvmap);
    log_replay();   // decrypts values as it reads — exits if wrong password
    log_open();

    printf("Launched Ruth Key - Value Store\n");
    printf("Type 'help' for commands.\n\n");

    char line[512];
    char key[256], value[256];

    printf("db> ");
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';

        if (sscanf(line, "put %255s %255s", key, value) == 2) {
            db_put(key, value);
            log_write_put(key, value);
            printf("OK\n");

        } else if (sscanf(line, "get %255s", key) == 1) {
            const char *v = db_get(key);
            if (v) printf("%s\n", v);
            else   printf("(nil)\n");

        } else if (sscanf(line, "delete %255s", key) == 1) {
            db_delete(key);
            log_write_del(key);
            printf("Done...\n");

        } else if (strcmp(line, "compact") == 0) {
            log_compact();

        } else if (strcmp(line, "keys") == 0) {
            cmd_keys();

        } else if (strcmp(line, "help") == 0) {
            print_help();

        } else if (strcmp(line, "exit") == 0) {
            break;

        } else if (strlen(line) > 0) {
            printf("Unknown command. Type 'help' for commands.\n");
        }

        printf("db> ");
    }

    db_free_all();
    if (log_fp) fclose(log_fp);
    printf("\nDone.\n");
    return 0;
}