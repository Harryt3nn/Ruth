#ifndef WAL_H
#define WAL_H

#include <stdint.h>
#include <stdio.h>

// ─── WAL entry types ──────────────────────────────────────────────────────────
typedef enum {
    WAL_PUT,
    WAL_DEL
} WalOp;

typedef struct {
    uint64_t index;        // Raft log index
    uint64_t term;         // Raft term when written
    WalOp    op;
    char     key[256];
    char     value[1024];
} WalEntry;

typedef struct {
    FILE    *fp;
    char     path[256];
    uint64_t last_index;
} Wal;

// ─── API ──────────────────────────────────────────────────────────────────────
int  wal_open(Wal *w, const char *path);
int  wal_append(Wal *w, WalEntry *e);
int  wal_read_all(Wal *w, void (*cb)(const WalEntry*, void*), void *userdata);
int  wal_truncate_after(Wal *w, uint64_t index);
void wal_close(Wal *w);

#endif