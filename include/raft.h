#ifndef RAFT_H
#define RAFT_H
#include "wal.h"
#include "cluster.h"
#include <stdint.h>
#include <pthread.h>
#include <time.h>


// Raft roles 
typedef enum // nodes cycle between these roles
{
    RAFT_FOLLOWER, // default state of node
    RAFT_CANDIDATE, // starts an election, requests votes (if wins an election becomes leader, else becomes follower)
    RAFT_LEADER // only one leader at a time, sends heartbeats
} RaftRole;


// Raft message types (sent between nodes) 
typedef enum 
{
    RAFT_MSG_VOTE_REQ, // used in elections
    RAFT_MSG_VOTE_RESP,
    RAFT_MSG_APPEND_REQ, // heartbeat + log entries
    RAFT_MSG_APPEND_RESP
} RaftMsgType;


typedef struct 
{
    RaftMsgType type; // RPC type
    uint64_t term; // raft term
    int from_id; // id of sender node


    // VOTE_REQ fields (prevents old leaders being relected)
    uint64_t last_log_index;
    uint64_t last_log_term;

    // VOTE_RESP fields
    int vote_granted;

    // APPEND_REQ fields
    uint64_t prev_log_index;
    uint64_t prev_log_term;
    uint64_t leader_commit;
    int entry_count;
    WalEntry entries[8]; // batch up to 8 entries per message

    // APPEND_RESP fields
    int success;
    uint64_t match_index;
} RaftMsg;

// Per-peer replication state (leader only)
typedef struct 
{
    int node_id;
    uint64_t next_index;   // next entry to send to this peer
    uint64_t match_index;  // highest entry known replicated on peer
} PeerState;

// Core Raft state (persistant and volitiles)
typedef struct 
{
    RaftRole role;
    int self_id;

    // Persistent state
    uint64_t current_term; // highest term seen 
    int voted_for; // -1 = none; who was voted for in this election
    Wal wal; // the raft log

    // Volatile state
    uint64_t commit_index;
    uint64_t last_applied;

    // Leader volatile state
    PeerState peers[MAX_NODES];
    int peer_count;

    // Election
    int votes_received;
    time_t last_heartbeat;
    int election_timeout_ms;  // randomised 150-300ms

    // Apply callback — called when entries are committed
    void (*apply_fn)(const WalEntry *e, void *userdata);
    void *apply_userdata;

    Cluster *cluster;
    pthread_mutex_t lock;
    pthread_t ticker_thread;
    int running;
} RaftState;

// API functions:
int raft_init(RaftState *r, int self_id, Cluster *c, void (*apply_fn)(const WalEntry*, void*), void *userdata);
void raft_start(RaftState *r);
void raft_stop(RaftState *r);

// Submit a new entry (leader only — returns 0 if not leader)
int  raft_submit(RaftState *r, WalOp op, const char *key, const char *value);

// Handle incoming Raft message from peer
void raft_handle_message(RaftState *r, RaftMsg *msg, int reply_fd);

int  raft_is_leader(const RaftState *r);
int  raft_leader_id(const RaftState *r);

#endif