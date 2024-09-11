#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define TCP_PORT 5100

int main(int argc, char **argv)
{
    int ssock;
    struct sockaddr_in servaddr;
    char mesg[BUFSIZ];
   
    if(argc < 2) {
        printf("Usage : %s IP_ADRESS\n", argv[0]);
        return -1;
    }

    if((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    
    inet_pton(AF_INET, argv[1], &(servaddr.sin_addr.s_addr));
    servaddr.sin_port = htons(TCP_PORT);

    if(connect(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect()");
        return -1;
    }

    while (1) {
        printf("메시지를 입력하세요 (종료하려면 'q' 입력): ");
        fgets(mesg, BUFSIZ, stdin);

        if(send(ssock, mesg, strlen(mesg), 0) <= 0) {
            perror("send()");
            break;
        }

        if (strncmp(mesg, "q", 1) == 0) {
            printf("채팅을 종료합니다.\n");
            break;
        }

        memset(mesg, 0, BUFSIZ);
        if(recv(ssock, mesg, BUFSIZ, 0) <= 0) {
            perror("recv()");
            break;
        }

        printf("서버로부터 받은 메시지: %s", mesg);
    }

    close(ssock);
    return 0;
}

