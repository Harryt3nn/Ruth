#ifndef HEARTBEAT_H
#define HEARTBEAT_H
#include "cluster.h"
#include <pthread.h>
#define HEARTBEAT_INTERVAL_MS 500 // 500 ms interval between heartbeats
#define DEAD_THRESHOLD_MS 1500 // if node has been silent for 3 times heartbeat interval, it is considered dead

typedef struct 
{
    Cluster *cluster; // cluster state pointer 
    int running; // flag
    pthread_t thread; // Last time we heard from each node (ms timestamp)
    long last_seen[MAX_NODES]; // last hearbeat stored here
} HeartbeatMonitor;

void heartbeat_start(HeartbeatMonitor *hb, Cluster *c);
void heartbeat_stop(HeartbeatMonitor *hb);
void heartbeat_update(HeartbeatMonitor *hb, int node_id);

#endif