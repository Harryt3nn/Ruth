#include<stdio.h>
#include<khash.h>
#include<string.h>
#include<CLI.h>

void ruthCmd()
{
    printf("\nruth db>  'ruth.key'   returns list of all keys");
    printf("\nruth db>  'ruth.set'   set a new Key-Value store");
    printf("\nruth db>  'ruth.get'   fetch a value");
    printf("\nruth db>  'ruth.del'   fetch a value");
    printf("\nruth db>  'ruth.ext'   close ruth");
}
void ruthKey()
{
    printf("\nRUTH KEY PROTOCOL");
}
void ruthDel()
{
    printf("\nRUTH DEL PROTOCOL");
}
void ruthSet()
{
    printf("\nRUTH SET PROTOCOL");
}
void ruthGet()
{
    printf("\nRUTH GET PROTOCOL");
}
void ruthExt()
{
    printf("\n RUTH EXT PROTOCOL");
}
int main(int argc, char *argv[]) 
{
    char line[512], key[256], value[256], postSpace[256];
    char *commands[] = {"ruth.key", "ruth.ext", "ruth.cmd", "ruth.set ", "ruth.get ", "ruth.del ", "unknown"};
    printf("\nruth db> type 'ruth.cmd' for commands");
    while(1)
    {
        printf("\nruth db> "); fgets(line, sizeof(line), stdin); line[strcspn(line, "\n")] = 0;
        if (strcmp(line, commands[0]) == 0){ruthKey();}
        else if (strcmp(line, commands[1]) == 0){ruthExt();}
        else if (strcmp(line, commands[2]) == 0){ruthCmd();}
        else if (strncmp(line, commands[5], strlen(commands[5])) == 0)
        {
            if (sscanf(line + strlen(commands[5]), "%255s" "%255s", key, postSpace) == 1){ruthDel(); }
            else {printf("\nruth db> Not a valid input...");}
        } 
        else if (strncmp(line, commands[4], strlen(commands[4])) == 0)
        {
            if (sscanf(line + strlen(commands[4]), "%255s" "%255s", key, postSpace) == 1){ruthGet();}
            else {printf("\nruth db> Not a valid input...");}
        }
        else if (strncmp(line, commands[3], strlen(commands[3])) == 0)
        {
            if (sscanf(line + strlen(commands[3]), "%255s" "%255s" "%255s", key, value, postSpace) == 2){ruthSet();}
            else {printf("\nruth db> Not a valid input...");}
        }
        else {printf("\nruth db> Not a valid input...");}
    }
}