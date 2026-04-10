#define _POSIX_C_SOURCE 200809L
#include "heartbeat.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

// failure detector for the cluster of nodes
// every node sends periodic heartbeat messages 
// this ensures nodes are active, or 'alive'

static long now_ms() // monotonic clock
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}


static void *monitor_thread(void *arg) 
{
    // thread wakes every heartbeat interval
    HeartbeatMonitor *hb = arg;
    while (hb->running) 
    {
        usleep(HEARTBEAT_INTERVAL_MS * 1000);
        long now = now_ms();


        for (int i = 0; i < hb->cluster->count; i++) // iterates for all nodes
        {
            Node *n = &hb->cluster->nodes[i];
            if (hb->last_seen[i] == 0) continue; // (can skip inactive nodes)
            long silent = now - hb->last_seen[i]; // silence duration for nodes

            // detect dead nodes (inactivity for 3x heartbeat interval duration)
            if (silent > DEAD_THRESHOLD_MS && n->status == NODE_ALIVE) 
            {
                cluster_mark_dead(hb->cluster, n->id);
                printf("[heartbeat] node %d declared DEAD (%ldms silent)\n",n->id, silent);
            } 

            // dead nodes can be brought back from the dead!!
            else if (silent <= DEAD_THRESHOLD_MS && n->status == NODE_DEAD) 
            {
                cluster_mark_alive(hb->cluster, n->id);
                printf("[heartbeat] node %d is ALIVE again\n", n->id);
            }
        }
    }
    return NULL;
}


void heartbeat_start(HeartbeatMonitor *hb, Cluster *c) 
{
    memset(hb, 0, sizeof(HeartbeatMonitor)); // zeroises the heartbeat monitor struct
    hb->cluster = c; // initialise the thread pointer
    hb->running = 1; // monitor marked as running (node is alive)
    pthread_create(&hb->thread, NULL, monitor_thread, hb); 
}

void heartbeat_stop(HeartbeatMonitor *hb) 
{
    hb->running = 0;
    pthread_join(hb->thread, NULL); // signals thread to exit
}

void heartbeat_update(HeartbeatMonitor *hb, int node_id) // call when heartbeat arrives from node
{
    for (int i = 0; i < hb->cluster->count; i++) // search for node in cluster
        if (hb->cluster->nodes[i].id == node_id) 
        {
            hb->last_seen[i] = now_ms(); // update last seen, as a new update has just been sent
            return;
        }
}