#include<stdio.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<unistd.h>

struct sockaddr_in server_address;
int client_socket; 
int port = 9002;
int flag = 0;
char host[64] = "127.0.0.1"

void main()
{

}

void close_socket(client_socket)
{
    close(client_socket);
}

void open_socket(client_socket, port, flag, host)
{
    client_socket = socket(AF_INET, SOCK_STREAM, flag);
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = inet_addr(host);
}

void check_connection()
{
    int connection_status = connect(client_socket, (struct sockaddr *) &server_address, sizeof(server_address));
    if (connection_status == -1)
    {
        printf("\nruth db>  Client failed to connect");
    }
}