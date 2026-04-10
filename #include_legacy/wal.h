#ifndef WAL_H
#define WAL_H
#include <stdint.h>
#include <stdio.h>

// WAL entry types
typedef enum 
{
    WAL_PUT, // set key + value
    WAL_DEL // delete key 
} WalOp;

typedef struct // an instance of a raft log entry
{
    uint64_t index; // Raft log index
    uint64_t term; // Raft term when written
    WalOp op; // operation (put/del)
    char key[256]; // key being modified
    char value[1024]; // value being modified (for put op)
} WalEntry;

typedef struct 
{
    FILE *fp; //open file for appending
    char path[256]; //path to said file
    uint64_t last_index; // highest raft log index
} Wal;


// API functions:

int  wal_open(Wal *w, const char *path);
int  wal_append(Wal *w, WalEntry *e);
int  wal_read_all(Wal *w, void (*cb)(const WalEntry*, void*), void *userdata);
int  wal_truncate_after(Wal *w, uint64_t index);
void wal_close(Wal *w);

#endif