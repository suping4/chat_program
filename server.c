#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <fcntl.h>

#define TCP_PORT 5100
#define MAX_ID_LEN 20
#define MAX_PW_LEN 20

struct LoginInfo {
    char id[MAX_ID_LEN];
    char password[MAX_PW_LEN];
};

struct Message {
    char id[MAX_ID_LEN];
    char content[BUFSIZ];
};

int main(int argc, char **argv)
{
    int ssock; /* 소켓 디스크립트 정의 */
    int pipe_fd[2]; // 파이프 디스크립터 정의
    socklen_t clen;
    pid_t pid;
    int n;  // 데이터 전송 및 수신 변수
    struct sockaddr_in servaddr, cliaddr; /* 주소 구조체 정의 */
    char mesg[BUFSIZ];

    /* 서버 소켓 생성 */
    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("socket()");
        return -1;
    }

    /* 파이프 생성 */
    if (pipe(pipe_fd) < 0) {
        perror("pipe()");
        return -1;
    }

    /* 주소 구조체에 주소 지정 */
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(TCP_PORT); /* 사용할 포트 지정 */

    /* bind 함수를 사용하여 서버 소켓의 주소 설정 */
    if (bind(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        perror("bind()");
        return -1;
    }

    /* 동시에 접속하는 클라이언트의 처리를 위한 대기 큐를 설정 */
    if (listen(ssock, 8) < 0){
        perror("listen()");
        return -1;
    }

    printf("서버가 시작되었습니다. 포트 %d\n", TCP_PORT);

    while (1) {
        clen = sizeof(cliaddr);
        int csock = accept(ssock, (struct sockaddr *)&cliaddr, &clen);
        if (csock < 0) {
            perror("accept()");
            continue;
        }

        /* 네트워크 주소를 문자열로 변경 */
        inet_ntop(AF_INET, &cliaddr.sin_addr, mesg, BUFSIZ);
        printf("클라이언트 연결됨: %s\n", mesg);

        if ((pid = fork()) < 0) {
            perror("fork()");
        } else if (pid == 0) {
            // 자식 프로세스
            close(ssock);
            close(pipe_fd[0]); // 파이프 읽기 닫기

            // 로그인 정보 수신
            struct LoginInfo login;
            if (recv(csock, &login, sizeof(login), 0) <= 0) {
                perror("로그인 정보 수신 실패");
                exit(1);
            }

            // 무조건 로그인 성공
            strcpy(mesg, "로그인 성공");

            // 인증 결과 전송
            if (send(csock, mesg, strlen(mesg), 0) <= 0) {
                perror("인증 결과 전송 실패");
                exit(1);
            }

            printf("사용자 '%s' 로그인 성공\n", login.id);

            while (1) {
                struct Message msg;
                memset(&msg, 0, sizeof(msg));
                // 클라이언트로부터 메시지 읽기
                if ((n = recv(csock, &msg, sizeof(msg), 0)) <= 0){
                    perror("클라이언트로부터 recv() 실패");
                    break;
                }
                printf("클라이언트로부터 받은 메시지: %s: %s", msg.id, msg.content);

                // 부모 프로세스에게 메시지 전달
                if (write(pipe_fd[1], &msg, sizeof(msg)) <= 0) {
                    perror("파이프에 write() 실패");
                    break;
                }

                // 클라이언트에게 응답
                if (send(csock, &msg, sizeof(msg), 0) <= 0) {
                    perror("클라이언트에게 send() 실패");
                    break;
                }

                if (strncmp(msg.content, "q", 1) == 0) {
                    printf("클라이언트 종료\n");
                    break;
                }
            }

            close(pipe_fd[1]);
            close(csock);
            exit(0);
        } else {
            // 부모 프로세스
            close(csock);
        }

        // 좀비 프로세스 처리
        while (waitpid(-1, NULL, WNOHANG) > 0);
    }

    close(pipe_fd[0]);
    close(pipe_fd[1]);
    close(ssock);
    return 0;
}
