#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include "cluster.h"
#include <pthread.h>

#define HEARTBEAT_INTERVAL_MS 500
#define DEAD_THRESHOLD_MS     1500

typedef struct {
    Cluster   *cluster;
    int        running;
    pthread_t  thread;
    // Last time we heard from each node (ms timestamp)
    long       last_seen[MAX_NODES];
} HeartbeatMonitor;

void heartbeat_start(HeartbeatMonitor *hb, Cluster *c);
void heartbeat_stop(HeartbeatMonitor *hb);
void heartbeat_update(HeartbeatMonitor *hb, int node_id);

#endif