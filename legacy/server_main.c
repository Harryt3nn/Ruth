#include "store.h"
#include "protocol.h"
#include "raft.h"
#include "cluster.h"
#include "heartbeat.h"
#include "crypto.h"
#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define BUF_SIZE 4096
#include <libgen.h>

static int server_fd = -1;
static Cluster cluster;
static RaftState raft;
static HeartbeatMonitor heartbeat;

// Raft apply callback — called when entry is committed 
static void on_commit(const WalEntry *e, void *userdata) 
{
    (void)userdata;
    if (e->op == WAL_PUT) 
    {
        store_put(e->key, e->value);
    }
    else   
    {
        store_delete(e->key);
    }
}

// Per-client thread 
typedef struct 
{
    int fd;
    Session session;
    int handshook;
    int is_peer;   // 1 if this connection is from another Raft node
} ClientState;

static void dispatch(ClientState *cs, const Command *cmd) 
{
    int fd = cs->fd;

    if (!cs->handshook && cmd->type != CMD_HELLO) 
    {
        protocol_send_response(fd, RESP_ERROR, "send HELLO first");
        return;
    }

    switch (cmd->type) 
    {

        case CMD_HELLO: 
        {
            uint8_t client_pub[CURVE25519_KEY_LEN];
            if (!crypto_from_hex(cmd->pubkey_hex, client_pub, CURVE25519_KEY_LEN)) 
            {
                protocol_send_response(fd, RESP_ERROR, "bad pubkey"); return;
            }
            if (!session_init(&cs->session)) 
            {
                protocol_send_response(fd, RESP_ERROR, "keygen failed"); return;
            }
            if (!crypto_derive_session(cs->session.keypair.priv, client_pub, &cs->session.key)) 
            {
                protocol_send_response(fd, RESP_ERROR, "key derivation failed"); return;
            }
            char pub_hex[65];
            crypto_to_hex(cs->session.keypair.pub, CURVE25519_KEY_LEN, pub_hex);
            protocol_send_response(fd, RESP_HELLO, pub_hex);
            cs->handshook = 1;
            break;
        }

        case CMD_ROTATE:
            session_invalidate(&cs->session);
            cs->handshook = 0;
            protocol_send_response(fd, RESP_OK, NULL);
            break;

        case CMD_PUT:
            // Forward write through Raft — only committed writes hit the store
            if (!raft_is_leader(&raft)) 
            {
                char msg[128];
                snprintf(msg, sizeof(msg), "not leader, try node %d", raft_leader_id(&raft));
                protocol_send_response(fd, RESP_ERROR, msg);
            } 
            else 
            {
                raft_submit(&raft, WAL_PUT, cmd->key, cmd->value);
                protocol_send_response(fd, RESP_OK, NULL);
            }
            break;

        case CMD_GET: 
        {
            // Reads served locally — may be slightly stale on followers
            const char *val = store_get(cmd->key);
            if (val) 
            {
                protocol_send_response(fd, RESP_VALUE, val);
            }
            else 
            {
                protocol_send_response(fd, RESP_NIL, NULL);
            }    
            break;
        }

        case CMD_DEL:
            if (!raft_is_leader(&raft)) 
            {
                protocol_send_response(fd, RESP_ERROR, "not leader");
            } 
            else 
            {
                raft_submit(&raft, WAL_DEL, cmd->key, "");
                protocol_send_response(fd, RESP_OK, NULL);
            }
            break;

        case CMD_KEYS: 
        {
            char *keys = store_keys();
            protocol_send_response(fd, RESP_KEYS, keys);
            free(keys);
            break;
        }

        case CMD_PING:
            protocol_send_response(fd, RESP_PONG, NULL);
            break;

        case CMD_UNKNOWN:
            protocol_send_response(fd, RESP_ERROR, "unknown command");
            break;
    }
}

static void *client_thread(void *arg)
 {
    int fd = *(int*)arg;
    free(arg);

    ClientState cs;
    memset(&cs, 0, sizeof(cs));
    cs.fd = fd;

    char    buf[BUF_SIZE];
    char    line[BUF_SIZE];
    int     line_len = 0;
    ssize_t n;
    Command cmd;

    while ((n = read(fd, buf, sizeof(buf))) > 0) 
    {
        // Check if this looks like a raw Raft binary message
        if (!cs.handshook && !cs.is_peer && n == sizeof(RaftMsg)) 
        {
            RaftMsg *msg = (RaftMsg*)buf;
            if (msg->type <= RAFT_MSG_APPEND_RESP) 
            {
                cs.is_peer = 1;
                Node *peer = cluster_get_node(&cluster, msg->from_id);  
                if (peer && peer->fd < 0) peer->fd = fd;                
                raft_handle_message(&raft, msg, fd);
                heartbeat_update(&heartbeat, msg->from_id);
                continue;
            }
        }
        if (cs.is_peer) 
        {
            RaftMsg *msg = (RaftMsg*)buf;
            Node *peer = cluster_get_node(&cluster, msg->from_id);      
            if (peer && peer->fd < 0) peer->fd = fd;                    
            raft_handle_message(&raft, msg, fd);
            heartbeat_update(&heartbeat, msg->from_id);
            continue;
        }

        // Otherwise treat as client text protocol
        for (ssize_t i = 0; i < n; i++) 
        {
            if (buf[i] == '\n') 
            {
                line[line_len] = '\0';
                if (line_len > 0) 
                {
                    protocol_parse_command(line, &cmd);
                    dispatch(&cs, &cmd);
                }
                line_len = 0;
            } else if (line_len < BUF_SIZE - 1) 
            {
                line[line_len++] = buf[i];
            }
        }
    }

    session_invalidate(&cs.session);
    printf("[server] connection closed (fd=%d)\n", fd);
    close(fd);
    return NULL;
}

static void handle_sigint(int sig) 
{
    (void)sig;
    printf("\n[server] shutting down...\n");
    raft_stop(&raft);
    heartbeat_stop(&heartbeat);
    cluster_stop_reconnector(&cluster);  // ← add this
    if (server_fd != -1) 
    {
        close(server_fd);
    }
    store_shutdown();
    exit(0);
}

static void read_password(char *buf, int maxlen) 
{
    struct termios old, noecho;
    tcgetattr(STDIN_FILENO, &old);
    noecho = old;
    noecho.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &noecho);
    fgets(buf, maxlen, stdin);
    buf[strcspn(buf, "\n")] = '\0';
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    printf("\n");
}

int main(int argc, char *argv[]) 
{
    if (argc < 3) 
    {
        fprintf(stderr, "Usage: %s <node_id> <config_file>\n", argv[0]);
        return 1;
    }

    int self_id = atoi(argv[1]);

    signal(SIGINT, handle_sigint);

    char password[128];
    printf("Enter store password: ");
    fflush(stdout);
    read_password(password, sizeof(password));
    if (strlen(password) == 0) 
    {
        fprintf(stderr, "Error: empty password\n");
        return 1;
    }

    // Load cluster config
    if (!cluster_load_config(&cluster, argv[2], self_id)) 
    {
        fprintf(stderr, "Error: failed to load cluster config\n");
        return 1;
    }

    // Init store + Raft
    char config_copy[256];
    strncpy(config_copy, argv[2], sizeof(config_copy) - 1);
    char data_dir[256];
    snprintf(data_dir, sizeof(data_dir), "%s/../data", dirname(config_copy));

    store_init(password, data_dir);

    
    memset(password, 0, sizeof(password));

    raft_init(&raft, self_id, &cluster, on_commit, NULL);

    // Connect to peers then start Raft
    cluster_connect_peers(&cluster);
    cluster_start_reconnector(&cluster);
    raft_start(&raft);
    heartbeat_start(&heartbeat, &cluster);

    // Find our port from config
    Node *self = cluster_get_node(&cluster, self_id);
    if (!self) 
    { 
        fprintf(stderr, "Node %d not in config\n", self_id); return 1; 
    }

    struct sockaddr_in addr;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(self->port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) 
    {
        perror("bind"); return 1;
    }
    listen(server_fd, 16);
    printf("[server] node %d listening on port %d\n", self_id, self->port);

    while (1) 
    {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (client_fd < 0) 
        {
            break;
        }
        printf("[server] connection from %s\n", inet_ntoa(client_addr.sin_addr));
        int *fd_ptr = malloc(sizeof(int));
        *fd_ptr = client_fd;
        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&tid, &attr, client_thread, fd_ptr);
        pthread_attr_destroy(&attr);
    }

    raft_stop(&raft);
    heartbeat_stop(&heartbeat);
    store_shutdown();
    return 0;
}