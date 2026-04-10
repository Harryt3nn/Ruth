#include <stdio.h>
#include <khash.h>
#include <string.h>
void help()
{
    printf("\nruth db>  'ruth.keys'  returns list of all keys");
    printf("\nruth db>  'ruth.put'   set a new Key-Value store");
    printf("\nruth db>  'ruth.get'   fetch a value");
    printf("\nruth db>  'ruth.exit'  close ruth");
}
void ruthKeys()
{
    printf("\nRUTH KEYS PROTOCOL");
}
void ruthPut()
{
    printf("\nRUTH PUT PROTOCOL");
}
void ruthGet()
{
    printf("\nRUTH GET PROTOCOL");
}
void ruthExit()
{
    printf("\n RUTH EXIT PROTOCOL");
}
void unknown()
{
    printf("\nruth db> Not a valid input...");
}
int main(int argc, char *argv[]) 
{
    char line[512], key[256], value[256];
    char *commands[] = {"ruth.keys", "ruth.put", "ruth.get", "ruth.exit", "help", "unknown"};
    printf("\nruth db> type 'help' for commands");
    while(1)
    {
        printf("\nruth db> ");
        fgets(line, sizeof(line), stdin);
        if (strcmp(line, commands[1])){ruthKeys();}
        else if (strcmp(line, commands[3])){ruthExit();}
        else if (strcmp(line, commands[4])){help();}
        else { unknown();}
    }
}