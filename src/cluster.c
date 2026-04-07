#include "cluster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


// defines cluster membership layer:
// system needs to know which nodes exist, where they exist (port), their status (determined by heartbeat)
// must know which key is owned by which node

// FNV-1a hash — fast, good distribution for consistent hashing 
static uint32_t fnv1a(const char *key) 
{
    uint32_t hash = 2166136261u;
    while (*key) 
    {
        hash ^= (uint8_t)*key++;
        hash *= 16777619u;
    }
    return hash;
}

// Virtual nodes per physical node (improves key distribution) 
#define VNODES_PER_NODE 150

typedef struct 
{
    uint32_t hash;
    int node_id;
} VNode;

static VNode ring[MAX_NODES * VNODES_PER_NODE];
static int ring_size = 0;

static int vnode_cmp(const void *a, const void *b) 
{
    return (((VNode*)a)->hash > ((VNode*)b)->hash) - (((VNode*)a)->hash < ((VNode*)b)->hash);
}

static void build_ring(Cluster *c) 
{
    ring_size = 0;
    char buf[128];
    for (int i = 0; i < c->count; i++) 
    {
        if (c->nodes[i].status == NODE_DEAD) 
        {
            continue;
        }
        for (int v = 0; v < VNODES_PER_NODE; v++) 
        {
            snprintf(buf, sizeof(buf), "%d-vnode-%d", c->nodes[i].id, v);
            ring[ring_size].hash = fnv1a(buf);
            ring[ring_size].node_id = c->nodes[i].id;
            ring_size++;
        }
    }
    qsort(ring, ring_size, sizeof(VNode), vnode_cmp);
}

// Config loading
int cluster_load_config(Cluster *c, const char *path, int self_id) 
{
    memset(c, 0, sizeof(Cluster));
    c->self_id = self_id;

    FILE *f = fopen(path, "r");
    if (!f) 
    { 
        perror("cluster config"); return 0; 
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) 
    {
        if (line[0] == '#' || line[0] == '\n') 
        {
            continue;
        }
        Node *n = &c->nodes[c->count];
        if (sscanf(line, "%d %63s %d", &n->id, n->host, &n->port) != 3) 
        {
            continue;
        }
        n->status = NODE_ALIVE;
        n->fd = -1;
        c->count++;
        if (c->count >= MAX_NODES)
        {
            break;
        } 
    }
    fclose(f);
    build_ring(c);
    return c->count > 0;
}

Node *cluster_get_node(Cluster *c, int id) 
{
    for (int i = 0; i < c->count; i++)
    {
        if (c->nodes[i].id == id) 
        {
            return &c->nodes[i];
        }
    }
    return NULL;
}

int cluster_hash_key(const Cluster *c, const char *key) 
{
    (void)c;
    if (ring_size == 0) 
    {
        return -1;
    }
    uint32_t h = fnv1a(key);
    // Binary search for first vnode >= h
    int lo = 0, hi = ring_size - 1, result = 0;
    while (lo <= hi) 
    {
        int mid = (lo + hi) / 2;
        if (ring[mid].hash >= h) 
        { 
            result = mid; hi = mid - 1; 
        }
        else 
        {
            lo = mid + 1;
        }
    }
    return ring[result % ring_size].node_id;
}

Node *cluster_node_for_key(Cluster *c, const char *key) 
{
    int id = cluster_hash_key(c, key);
    return cluster_get_node(c, id);
}

void cluster_mark_dead(Cluster *c, int id) 
{
    Node *n = cluster_get_node(c, id);
    if (n) 
    { 
        n->status = NODE_DEAD; build_ring(c); 
    }
}

void cluster_mark_alive(Cluster *c, int id) 
{
    Node *n = cluster_get_node(c, id);
    if (n) 
    { 
        n->status = NODE_ALIVE; build_ring(c);
    }
}

int cluster_quorum(const Cluster *c) 
{
    return (c->count / 2) + 1;  // always based on total, not alive
}

int cluster_connect_peers(Cluster *c) 
{
    for (int i = 0; i < c->count; i++) 
    {
        Node *n = &c->nodes[i];
        if (n->id == c->self_id || n->fd >= 0) 
        {
            continue;
        }
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(n->port);
        inet_pton(AF_INET, n->host, &addr.sin_addr);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) 
        {
            n->fd = fd;
            printf("[cluster] connected to node %d (%s:%d)\n", n->id, n->host, n->port);
        } 
        else 
        {
            close(fd);
            cluster_mark_dead(c, n->id);
            printf("[cluster] node %d unreachable\n", n->id);
        }
    }
    return 1;
}