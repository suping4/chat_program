#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <signal.h>

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

int ssock;
int pipe_fd[2];
pid_t child_pid;

void sigint_handler(int signo) {
    close(ssock);
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    if (child_pid > 0) {
        kill(child_pid, SIGTERM);
    }
    exit(0);
}

int main(int argc, char **argv) {
    struct sockaddr_in servaddr;
    char mesg[BUFSIZ];
    struct LoginInfo login;
   
    if (argc < 2) {
        printf("Usage : %s IP_ADRESS\n", argv[0]);
        return -1;
    }

    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    
    inet_pton(AF_INET, argv[1], &(servaddr.sin_addr.s_addr));
    servaddr.sin_port = htons(TCP_PORT);

    if (connect(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect()");
        return -1;
    }

    printf("아이디를 입력하세요: ");
    fgets(login.id, MAX_ID_LEN, stdin);
    login.id[strcspn(login.id, "\n")] = 0;

    printf("비밀번호를 입력하세요: ");
    fgets(login.password, MAX_PW_LEN, stdin);
    login.password[strcspn(login.password, "\n")] = 0;

    if (send(ssock, &login, sizeof(login), 0) <= 0) {
        perror("send()");
        return -1;
    }

    memset(mesg, 0, BUFSIZ);
    if (recv(ssock, mesg, BUFSIZ, 0) <= 0) {
        perror("recv()");
        return -1;
    }

    if (strcmp(mesg, "로그인 성공") != 0) {
        printf("로그인 실패: %s\n", mesg);
        close(ssock);
        return -1;
    }

    printf("로그인 성공! 채팅을 시작합니다.\n");

    if (pipe(pipe_fd) < 0) {
        perror("pipe()");
        return -1;
    }

    signal(SIGINT, sigint_handler);

    child_pid = fork();

    if (child_pid < 0) {
        perror("fork()");
        return -1;
    } else if (child_pid == 0) {
        // 자식 프로세스: 메시지 수신
        close(pipe_fd[1]);  // 쓰기 파이프 닫기
        while (1) {
            struct Message received_msg;
            memset(&received_msg, 0, sizeof(received_msg));
            if (recv(ssock, &received_msg, sizeof(received_msg), 0) <= 0) {
                perror("recv()");
                break;
            }
            printf("\n받은 메시지: %s: %s", received_msg.id, received_msg.content);
            write(pipe_fd[0], "1", 1);  // 부모에게 메시지 수신 알림
        }
        close(pipe_fd[0]);
        exit(0);
    } else {
        // 부모 프로세스: 메시지 송신
        close(pipe_fd[0]);  // 읽기 파이프 닫기
        while (1) {
            struct Message msg;
            strcpy(msg.id, login.id);
            
            printf("메시지를 입력하세요 (종료하려면 'q' 입력): ");
            fgets(msg.content, BUFSIZ, stdin);
            msg.content[strcspn(msg.content, "\n")] = 0;  // 개행 문자 제거

            if (strncmp(msg.content, "q", 1) == 0) {
                printf("채팅을 종료합니다.\n");
                break;
            }

            // 메시지 전송 전 클라이언트에서 출력
            printf("%s: %s\n", msg.id, msg.content);

            if (send(ssock, &msg, sizeof(msg), 0) <= 0) {
                perror("send()");
                break;
            }

            // 자식 프로세스의 메시지 수신 대기
            char buf[2];
            read(pipe_fd[1], buf, 1);
        }
        close(pipe_fd[1]);
    }

    close(ssock);
    kill(child_pid, SIGTERM);
    wait(NULL);
    return 0;
}

