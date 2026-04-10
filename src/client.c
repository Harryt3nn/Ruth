// Source - https://stackoverflow.com/q/18489271
// Posted by user2725511, modified by community. See post 'Timeline' for change history
// Retrieved 2026-04-07, License - CC BY-SA 3.0

#include<stdio.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/socket.h>
#include <errno.h>
#include<string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(void)
{
    int byte_count;
    struct sockaddr_in serveraddr;
    char *servername;
    char buf[256];
    socklen_t addr_size;
    int sockfd;

    sockfd=socket(AF_INET,SOCK_STREAM,0);
    bzero(&serveraddr,sizeof(serveraddr));
    serveraddr.sin_family=AF_INET;
    serveraddr.sin_port=htons(11378);
    servername=gethostbyname("localhost");
    inet_pton(AF_INET,servername,&serveraddr.sin_addr);

    addr_size=sizeof(serveraddr);
    if(connect(sockfd,(struct sockaddr *)&serveraddr,addr_size)==-1)
    {
        perror("connect");
        exit(1);
    }

    byte_count = recv(sockfd, buf, sizeof buf, 0);
    printf("recv()'d %d bytes of data in buf\n", byte_count);

    close(sockfd);
}
