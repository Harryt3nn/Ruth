#define _POSIX_C_SOURCE 200809L
#include "heartbeat.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static long now_ms() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void *monitor_thread(void *arg) {
    HeartbeatMonitor *hb = arg;
    while (hb->running) {
        usleep(HEARTBEAT_INTERVAL_MS * 1000);
        long now = now_ms();
        for (int i = 0; i < hb->cluster->count; i++) {
            Node *n = &hb->cluster->nodes[i];
            if (hb->last_seen[i] == 0) continue;
            long silent = now - hb->last_seen[i];
            if (silent > DEAD_THRESHOLD_MS && n->status == NODE_ALIVE) {
                cluster_mark_dead(hb->cluster, n->id);
                printf("[heartbeat] node %d declared DEAD (%ldms silent)\n",
                       n->id, silent);
            } else if (silent <= DEAD_THRESHOLD_MS && n->status == NODE_DEAD) {
                cluster_mark_alive(hb->cluster, n->id);
                printf("[heartbeat] node %d is ALIVE again\n", n->id);
            }
        }
    }
    return NULL;
}

void heartbeat_start(HeartbeatMonitor *hb, Cluster *c) {
    memset(hb, 0, sizeof(HeartbeatMonitor));
    hb->cluster = c;
    hb->running = 1;
    pthread_create(&hb->thread, NULL, monitor_thread, hb);
}

void heartbeat_stop(HeartbeatMonitor *hb) {
    hb->running = 0;
    pthread_join(hb->thread, NULL);
}

void heartbeat_update(HeartbeatMonitor *hb, int node_id) {
    for (int i = 0; i < hb->cluster->count; i++)
        if (hb->cluster->nodes[i].id == node_id) {
            hb->last_seen[i] = now_ms();
            return;
        }
}