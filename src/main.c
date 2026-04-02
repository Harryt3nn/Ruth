#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include "khash.h"
// hash map
KHASH_MAP_INIT_STR(kvmap, char*)

// globals
static khash_t(kvmap) *h;
static FILE *log_fp;
static const char *LOG_PATH = "db.log";

// hash table operations
void db_put(const char *key, const char *value) {
    int ret;
    khint_t k = kh_put(kvmap, h, key, &ret);

    if (ret == -1) 
    {
        fprintf(stderr, "Error: failed to insert key '%s'\n", key);
        return;
    }

    if (ret == 0) 
    {
        // Key already existed — free old value, but key pointer is reused
        free((char*)kh_val(h, k));
    } else 
    {
        // New key — duplicate it so we own the memory
        kh_key(h, k) = strdup(key);
    }

    kh_val(h, k) = strdup(value);
}

const char *db_get(const char *key) 
{
    khint_t k = kh_get(kvmap, h, key);
    if (k == kh_end(h)) return NULL;
    return kh_val(h, k);
}

void db_delete(const char *key) 
{
    khint_t k = kh_get(kvmap, h, key);
    if (k != kh_end(h)) {
        free((char*)kh_key(h, k));
        free((char*)kh_val(h, k));
        kh_del(kvmap, h, k);
    }
}

void db_free_all() 
{
    khint_t k;
    for (k = kh_begin(h); k != kh_end(h); ++k) 
    {
        if (!kh_exist(h, k)) continue;
        free((char*)kh_key(h, k));
        free((char*)kh_val(h, k));
    }
    kh_destroy(kvmap, h);
}

// log operations
void log_open() 
{
    log_fp = fopen(LOG_PATH, "a");
    if (!log_fp) 
    {
        fprintf(stderr, "Error: could not open log file '%s'\n", LOG_PATH);
        exit(1);
    }
}

void log_write_put(const char *key, const char *value) 
{
    fprintf(log_fp, "PUT %s %s\n", key, value);
    fflush(log_fp);
}

void log_write_del(const char *key) 
{
    fprintf(log_fp, "DEL %s\n", key);
    fflush(log_fp);
}

// Replay the log on startup to rebuild in-memory state
void log_replay() 
{
    FILE *f = fopen(LOG_PATH, "r");
    if (!f) return;  // no log yet, fresh start

    char op[8], key[256], value[256];
    while (fscanf(f, "%7s", op) == 1) 
    {
        if (strcmp(op, "PUT") == 0) 
        {
            if (fscanf(f, "%255s %255s", key, value) == 2) 
            {
                db_put(key, value);
            }
        } else if (strcmp(op, "DEL") == 0) 
        {
            if (fscanf(f, "%255s", key) == 1) 
            {
                db_delete(key);
            }
        } else 
        {
            // Unknown op — skip the rest of the line
            fscanf(f, "%*[^\n]\n");
        }
    }
    fclose(f);
}

// Compact: rewrite log with only live keys, discarding old puts/deletes
void log_compact() {
    FILE *f = fopen("db.log.tmp", "w");
    if (!f) {
        fprintf(stderr, "Error: could not open temp file for compaction\n");
        return;
    }

    khint_t k;
    for (k = kh_begin(h); k != kh_end(h); ++k) {
        if (!kh_exist(h, k)) continue;
        fprintf(f, "PUT %s %s\n", kh_key(h, k), kh_val(h, k));
    }
    fclose(f);

    if (log_fp) { fclose(log_fp); log_fp = NULL; }

    remove(LOG_PATH);  // Windows requires this before rename

    if (rename("db.log.tmp", LOG_PATH) != 0) {
        fprintf(stderr, "Error: compaction rename failed\n");
    } else {
        printf("Compaction done.\n");  // only print on success
    }

    log_open();
}

// CLI (temp)
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

int main() {
    h = kh_init(kvmap);

    // Rebuild state from disk before accepting commands
    log_replay();
    log_open();

    printf("kvdb - simple key-value store\n");
    printf("Type 'help' for commands.\n\n");

    char line[512];
    char key[256], value[256];

    printf("db> ");
    while (fgets(line, sizeof(line), stdin)) {

        // Strip trailing newline
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
            printf("OK\n");

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
    fclose(log_fp);
    printf("\nBye.\n");
    return 0;
}