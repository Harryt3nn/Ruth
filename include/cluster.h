#ifndef CLUSTER_H
#define CLUSTER_H
#include <stdint.h>
#define MAX_NODES 16
#include <pthread.h>

typedef enum 
{
    NODE_UNKNOWN, // state for before connection, or before first heartbeat
    NODE_ALIVE, // consistent heartbeat (every 500ms)
    NODE_DEAD // node has not generated heartbeat for 1500ms
} NodeStatus;

typedef struct 
{
    int id; // each node requires a unique node id
    char host[64]; // where to connect node
    int port; // where to connect node 
    NodeStatus status; // alive / dead / unknown
    int fd; // persistent peer connection (-1 if not connected)
} Node;

typedef struct 
{
    Node nodes[MAX_NODES]; // array of nodes
    int count; // number of configured nodes
    int self_id; // which node is this process
    pthread_t reconnect_thread;
    int running;
} Cluster;

// API functions:
int   cluster_load_config(Cluster *c, const char *path, int self_id); // loads cluster config file
Node *cluster_get_node(Cluster *c, int id); // fetch pointer for node with given id
Node *cluster_node_for_key(Cluster *c, const char *key); // consistent hash
int   cluster_connect_peers(Cluster *c); // attempts TCP connection for all alive nodes besides self
void  cluster_mark_dead(Cluster *c, int id); // change status to dead
void  cluster_mark_alive(Cluster *c, int id); // change status to living
int   cluster_quorum(const Cluster *c);  // majority count needed
static void *reconnect_thread(void *arg);
// Consistent hash: returns node id responsible for key
int cluster_hash_key(const Cluster *c, const char *key);

#endif