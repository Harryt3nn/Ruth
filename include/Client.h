#ifndef CLIENT_H
#define CLIENT_H
#include<stdio.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/socket.h>
#include <netinet/in.h>

typedef struct {
    int network_socket;
    char[64] host;

}ruthCon

#endif