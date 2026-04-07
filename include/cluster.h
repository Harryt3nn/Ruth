#ifndef CLUSTER_H
#define CLUSTER_H
#include <stdint.h>
#define MAX_NODES 16

typedef enum {
    NODE_UNKNOWN,
    NODE_ALIVE,
    NODE_DEAD
} NodeStatus;

typedef struct {
    int        id;
    char       host[64];
    int        port;
    NodeStatus status;
    int        fd;          // persistent peer connection (-1 if not connected)
} Node;

typedef struct {
    Node nodes[MAX_NODES];
    int  count;
    int  self_id;           // which node is this process
} Cluster;

// ─── API ──────────────────────────────────────────────────────────────────────
int   cluster_load_config(Cluster *c, const char *path, int self_id);
Node *cluster_get_node(Cluster *c, int id);
Node *cluster_node_for_key(Cluster *c, const char *key); // consistent hash
int   cluster_connect_peers(Cluster *c);
void  cluster_mark_dead(Cluster *c, int id);
void  cluster_mark_alive(Cluster *c, int id);
int   cluster_quorum(const Cluster *c);  // majority count needed

// Consistent hash: returns node id responsible for key
int   cluster_hash_key(const Cluster *c, const char *key);

#endif