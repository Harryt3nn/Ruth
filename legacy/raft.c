#define _POSIX_C_SOURCE 200809L
#include "raft.h"
#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>


// a raft is a consensus algorithm that ensures nodes agree on the same entry log sequence
// commands leader election, crash recovery, commitment rules

// Helpers 
static int random_election_timeout() 
{
    return 150 + (rand() % 150);  // 150–300ms
}

static void send_msg(int fd, const RaftMsg *msg)
 {
    if (fd < 0) return;
    write(fd, msg, sizeof(RaftMsg));
}

static void send_to_node(RaftState *r, int node_id, const RaftMsg *msg) 
{
    Node *n = cluster_get_node(r->cluster, node_id);
    if (n) send_msg(n->fd, msg);
}

static long ms_since(struct timespec t) 
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - t.tv_sec) * 1000 + (now.tv_nsec - t.tv_nsec) / 1000000;
}

// Apply committed entries to the store 
static void apply_committed(RaftState *r) 
{
    while (r->last_applied < r->commit_index) 
    {
        r->last_applied++;
        // Re-read entry from WAL and call apply callback
        // (simplified: in production keep an in-memory log buffer)
        printf("[raft] applied entry %lu\n", r->last_applied);
    }
}

// Become follower 
static void become_follower(RaftState *r, uint64_t term) 
{
    r->role = RAFT_FOLLOWER;
    r->current_term = term;
    r->voted_for = -1;
    r->election_timeout_ms = random_election_timeout();  
    clock_gettime(CLOCK_MONOTONIC, (struct timespec*)&r->last_heartbeat);
}

// Become candidate, start election
static void start_election(RaftState *r) 
{
    r->role = RAFT_CANDIDATE;
    r->current_term++;
    r->voted_for = r->self_id;
    r->votes_received = 1;  // vote for self

    // NEW: randomize and reset election timer for this term
    r->election_timeout_ms = random_election_timeout();
    clock_gettime(CLOCK_MONOTONIC, (struct timespec*)&r->last_heartbeat);

    printf("[raft] node %d → CANDIDATE (term %lu)\n", r->self_id, r->current_term);

    RaftMsg req;
    memset(&req, 0, sizeof(req));
    req.type = RAFT_MSG_VOTE_REQ;
    req.term = r->current_term;
    req.from_id = r->self_id;
    req.last_log_index = r->wal.last_index;

    for (int i = 0; i < r->cluster->count; i++) {
        Node *n = &r->cluster->nodes[i];
        if (n->id == r->self_id || n->status == NODE_DEAD) {
            continue;
        }
        send_to_node(r, n->id, &req);
    }
}

// Become leader 
static void become_leader(RaftState *r) 
{
    r->role = RAFT_LEADER;
    printf("[raft] node %d → LEADER (term %lu)\n", r->self_id, r->current_term);

    // Initialise peer state
    r->peer_count = 0;
    for (int i = 0; i < r->cluster->count; i++) 
    {
        Node *n = &r->cluster->nodes[i];
        if (n->id == r->self_id) continue;
        r->peers[r->peer_count].node_id    = n->id;
        r->peers[r->peer_count].next_index  = r->wal.last_index + 1;
        r->peers[r->peer_count].match_index = 0;
        r->peer_count++;
    }
}

// Send heartbeat / append entries to all followers
static void send_heartbeats(RaftState *r) 
{
    RaftMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = RAFT_MSG_APPEND_REQ;
    msg.term = r->current_term;
    msg.from_id = r->self_id;
    msg.leader_commit = r->commit_index;
    msg.entry_count = 0;  // empty = heartbeat

    for (int i = 0; i < r->peer_count; i++) 
    {
        Node *n = cluster_get_node(r->cluster, r->peers[i].node_id);
        if (!n || n->status == NODE_DEAD) 
        {
            continue;
        }
        send_msg(n->fd, &msg);
    }
}

// Handle vote request 
static void handle_vote_req(RaftState *r, const RaftMsg *req, int reply_fd)
 {
    RaftMsg resp;
    memset(&resp, 0, sizeof(resp));
    resp.type = RAFT_MSG_VOTE_RESP;
    resp.term = r->current_term;
    resp.from_id = r->self_id;

    if (req->term < r->current_term) 
    {
        resp.vote_granted = 0;
    } else 
    {
        if (req->term > r->current_term) become_follower(r, req->term);
        int can_vote = (r->voted_for == -1 || r->voted_for == req->from_id);
        int log_ok = req->last_log_index >= r->wal.last_index;
        resp.vote_granted = can_vote && log_ok;
        if (resp.vote_granted) r->voted_for = req->from_id;
    }

    send_msg(reply_fd, &resp);
}

// Handle vote response 
static void handle_vote_resp(RaftState *r, const RaftMsg *resp) 
{
    if (r->role != RAFT_CANDIDATE) 
    {
        return;
    }
    if (resp->term > r->current_term) 
    { 
        become_follower(r, resp->term); return; 
    }
    if (!resp->vote_granted) 
    {
        return;
    }
    r->votes_received++;
    if (r->votes_received >= cluster_quorum(r->cluster))
    {
        become_leader(r);
    }
}

// Handle append entries (heartbeat or real entries) 
static void handle_append_req(RaftState *r, const RaftMsg *req, int reply_fd) 
{
    RaftMsg resp;
    memset(&resp, 0, sizeof(resp));
    resp.type = RAFT_MSG_APPEND_RESP;
    resp.term = r->current_term;
    resp.from_id = r->self_id;

    if (req->term < r->current_term) 
    {
        resp.success = 0;
        send_msg(reply_fd, &resp);
        return;
    }

    // Valid leader — reset election timeout
    become_follower(r, req->term);
    r->leader_id = req->from_id;

    // Append entries
    for (int i = 0; i < req->entry_count; i++) 
    {
        WalEntry e = req->entries[i];
        if (e.index <= r->wal.last_index)
        {
            wal_truncate_after(&r->wal, e.index - 1);
        }
        wal_append(&r->wal, &e);
        // Apply to store immediately
        if (r->apply_fn) 
        {
            r->apply_fn(&e, r->apply_userdata);
        }
    }

    // Update commit index
    if (req->leader_commit > r->commit_index) 
    {
        r->commit_index = req->leader_commit < r->wal.last_index? req->leader_commit : r->wal.last_index;
        apply_committed(r);
    }

    resp.success = 1;
    resp.match_index = r->wal.last_index;
    send_msg(reply_fd, &resp);
}

// Handle append response from follower 
static void handle_append_resp(RaftState *r, const RaftMsg *resp) 
{
    if (r->role != RAFT_LEADER) 
    {
        return;
    }
    if (resp->term > r->current_term) 
    { 
        become_follower(r, resp->term); return; 
    }

    // Find peer
    PeerState *peer = NULL;
    for (int i = 0; i < r->peer_count; i++)
    {
         if (r->peers[i].node_id == resp->from_id) 
        { 
            peer = &r->peers[i]; break; 
        }
    }
    if (!peer) 
    {
        return;
    }
    if (resp->success) 
    {
        peer->match_index = resp->match_index;
        peer->next_index  = resp->match_index + 1;

        // Check if we can advance commit_index
        // A log entry is committed once a majority have it
        for (uint64_t n = r->wal.last_index; n > r->commit_index; n--) 
        {
            int count = 1;  // leader itself
            for (int i = 0; i < r->peer_count; i++)
            {
                if (r->peers[i].match_index >= n)
                {
                     count++;
                }
            }
            if (count >= cluster_quorum(r->cluster))
             {
                r->commit_index = n;
                apply_committed(r);
                break;
            }
        }
    } 
    else 
    {
        // Decrement next_index and retry
        if (peer->next_index > 1) peer->next_index--;
    }
}

// Public: handle any incoming Raft message 
void raft_handle_message(RaftState *r, RaftMsg *msg, int reply_fd) {
    pthread_mutex_lock(&r->lock);
    switch (msg->type) 
    {
        case RAFT_MSG_VOTE_REQ: handle_vote_req(r, msg, reply_fd); break;
        case RAFT_MSG_VOTE_RESP: handle_vote_resp(r, msg); break;
        case RAFT_MSG_APPEND_REQ: handle_append_req(r, msg, reply_fd); break;
        case RAFT_MSG_APPEND_RESP: handle_append_resp(r, msg); break;
    }
    pthread_mutex_unlock(&r->lock);
}

// Ticker: runs in background thread 
static void *raft_ticker(void *arg) 
{
    RaftState *r = arg;
    while (r->running) 
    {
        usleep(10000);  // tick every 10ms
        pthread_mutex_lock(&r->lock);
        if (r->role == RAFT_LEADER) 
        {
            send_heartbeats(r);
        } 
        else 
        {
            // Check election timeout
            if (ms_since(r->last_heartbeat) > r->election_timeout_ms)
            {
                 start_election(r);
            }
        }
        pthread_mutex_unlock(&r->lock);
    }
    return NULL;
}

// Init / start / stop 
int raft_init(RaftState *r, int self_id, Cluster *c, void (*apply_fn)(const WalEntry*, void*), void *userdata) 
{
    memset(r, 0, sizeof(RaftState));
    r->self_id = self_id;
    r->cluster = c;
    r->role = RAFT_FOLLOWER;
    r->voted_for = -1;
    r->apply_fn = apply_fn;
    r->apply_userdata = userdata;
    r->election_timeout_ms = random_election_timeout();
    r->running = 0;

    pthread_mutex_init(&r->lock, NULL);

    char wal_path[128];
    snprintf(wal_path, sizeof(wal_path), "data/wal-%d.bin", self_id);
    return wal_open(&r->wal, wal_path);
}

void raft_start(RaftState *r) 
{
    r->running = 1;
    clock_gettime(CLOCK_MONOTONIC, (struct timespec*)&r->last_heartbeat);
    pthread_create(&r->ticker_thread, NULL, raft_ticker, r);
    printf("[raft] node %d started\n", r->self_id);
}

void raft_stop(RaftState *r) 
{
    r->running = 0;
    pthread_join(r->ticker_thread, NULL);
    wal_close(&r->wal);
    pthread_mutex_destroy(&r->lock);
}

int raft_submit(RaftState *r, WalOp op, const char *key, const char *value)
 {
    pthread_mutex_lock(&r->lock);
    if (r->role != RAFT_LEADER) 
    {
        pthread_mutex_unlock(&r->lock);
        return 0;
    }

    WalEntry e;
    memset(&e, 0, sizeof(e));
    e.term = r->current_term;
    e.op = op;
    strncpy(e.key, key, sizeof(e.key) - 1);
    strncpy(e.value, value, sizeof(e.value) - 1);
    wal_append(&r->wal, &e);

    // Ship to followers immediately
    RaftMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = RAFT_MSG_APPEND_REQ;
    msg.term = r->current_term;
    msg.from_id = r->self_id;
    msg.leader_commit = r->commit_index;
    msg.entry_count = 1;
    msg.entries[0] = e;

    for (int i = 0; i < r->peer_count; i++) 
    {
        Node *n = cluster_get_node(r->cluster, r->peers[i].node_id);
        if (n && n->status == NODE_ALIVE) send_msg(n->fd, &msg);
    }

    pthread_mutex_unlock(&r->lock);
    return 1;
}

int raft_is_leader(const RaftState *r) 
{ 
    return r->role == RAFT_LEADER; 
}

int raft_leader_id(const RaftState *r) 
{
    return r->role == RAFT_LEADER ? r->self_id : r->leader_id;
}