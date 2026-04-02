#ifndef STORE_H
#define STORE_H

void store_init(const char *password);
void store_shutdown();

void store_put(const char *key, const char *value);
const char *store_get(const char *key);
void store_delete(const char *key);
char *store_keys();   // returns heap-allocated newline-separated key list

#endif