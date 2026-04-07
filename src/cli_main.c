#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT  6379

static void print_help() 
{
    printf("Commands:\n");
    printf("  put <key> <value>   Store a value\n");
    printf("  get <key>           Retrieve a value\n");
    printf("  delete <key>        Delete a key\n");
    printf("  keys                List all keys\n");
    printf("  ping                Check server is alive\n");
    printf("  help                Show this message\n");
    printf("  exit                Disconnect and quit\n");
}

int main(int argc, char *argv[]) 
{
    const char *host = (argc > 1) ? argv[1] : DEFAULT_HOST;
    int port = (argc > 2) ? atoi(argv[2]) : DEFAULT_PORT;

    DbConn *conn = db_connect(host, port);
    if (!conn) 
    {
        fprintf(stderr, "Error: could not connect to %s:%d\n", host, port);
        fprintf(stderr, "Is the server running?\n");
        return 1;
    }

    printf("Connected to %s:%d\n", host, port);
    printf("Type 'help' for commands.\n\n");

    char line[4096];
    char key[256], value[1024];

    printf("db> ");
    while (fgets(line, sizeof(line), stdin)) 
    {
        line[strcspn(line, "\n")] = '\0';

        if (sscanf(line, "put %255s %1023[^\n]", key, value) == 2) 
        {
            printf(db_put(conn, key, value) ? "OK\n" : "ERROR\n");

        } 
        else if (sscanf(line, "get %255s", key) == 1 && strncmp(line, "get", 3) == 0) 
        {
            char *val = db_get(conn, key);
            if (val) 
            { 
                printf("%s\n", val); free(val); 
            }
            else       
            {
                printf("(nil)\n");
            }

        } 
        else if (sscanf(line, "delete %255s", key) == 1 && strncmp(line, "delete", 6) == 0)
        {
            printf(db_delete(conn, key) ? "OK\n" : "ERROR\n");
        } 
        else if (strcmp(line, "keys") == 0) 
        {
            char *keys = db_keys(conn);
            if (keys) 
            { 
                printf("%s", keys); free(keys); 
            }
            else        
            {
                printf("(empty)\n");
            }

        } 
        else if (strcmp(line, "ping") == 0) 
        {
            printf(db_ping(conn) ? "PONG\n" : "ERROR: no response\n");
        } 
        else if (strcmp(line, "help") == 0) 
        {
            print_help();
        } 
        else if (strcmp(line, "exit") == 0) 
        {
            break;
        } 
        else if (strlen(line) > 0) 
        {
            printf("Unknown command. Type 'help'.\n");
        }
        printf("db> ");
    }

    db_disconnect(conn);
    printf("\nDone...\n");
    return 0;
}