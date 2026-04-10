#ifndef STORE_H
#define STORE_H

void store_init(const char *password, const char *data_dir); // initialses in memory store
void store_shutdown(); // needed to wipe sensetive data


// when raft commits a(n) _____ entry 
void store_put(const char *key, const char *value); 
const char *store_get(const char *key);
void store_delete(const char *key);
char *store_keys(); // returns heap-allocated newline-separated key list

#endif