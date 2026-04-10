#include "wal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// WAL - write ahead logic
// WAL binary format per entry:
// [8 bytes index][8 bytes term][4 bytes op][256 bytes key][1024 bytes value]
#define ENTRY_SIZE (8 + 8 + 4 + 256 + 1024)

int wal_open(Wal *w, const char *path) //open or create wal file
{
    // store path and last recieved index 
    strncpy(w->path, path, sizeof(w->path) - 1);
    w->last_index = 0;

    // Read existing log to find last index (allows node to resume raft state after crash)
    FILE *f = fopen(path, "rb");
    if (f) 
    {
        WalEntry e;
        while (fread(&e, sizeof(WalEntry), 1, f) == 1)
            w->last_index = e.index;
        fclose(f);
    }

    w->fp = fopen(path, "ab"); // file footpath for appending
    if (!w->fp) 
    { 
        perror("wal_open"); return 0; 
    }
    return 1;
}


int wal_append(Wal *w, WalEntry *e) // append new identity
{
    e->index = ++w->last_index;
    if (fwrite(e, sizeof(WalEntry), 1, w->fp) != 1) // written as a binary blob
    {
        return 0;
    }
    fflush(w->fp); // if node crashes, entry is still saved to disk
    return 1;
}


int wal_read_all(Wal *w, void (*cb)(const WalEntry*, void*), void *userdata)
// wal replayed in node startup and crash recovery
 {
    FILE *f = fopen(w->path, "rb"); // wal opened in 'read mode'
    if (!f) 
    {
        return 0;
    }
    WalEntry e;
    while (fread(&e, sizeof(WalEntry), 1, f) == 1)
        cb(&e, userdata);
    fclose(f);
    return 1;
}


// Truncate everything after index — used when follower gets conflicting entries
int wal_truncate_after(Wal *w, uint64_t index) 
{
    FILE *f = fopen(w->path, "rb");
    if (!f) return 0;

    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", w->path);
    FILE *out = fopen(tmp, "wb");

    WalEntry e;
    while (fread(&e, sizeof(WalEntry), 1, f) == 1) 
    {
        if (e.index > index) break;
        fwrite(&e, sizeof(WalEntry), 1, out);
    }
    fclose(f);
    fclose(out);

    if (w->fp) fclose(w->fp);
    remove(w->path);
    rename(tmp, w->path);
    w->last_index = index;
    w->fp = fopen(w->path, "ab");
    return w->fp != NULL;
}


void wal_close(Wal *w) 
{
    if (w->fp) 
    { 
        fclose(w->fp); w->fp = NULL; 
    }
}