#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <syslog.h>
#include <sys/stat.h>

#define TCP_PORT 5100
#define MAX_ID_LEN 20
#define MAX_PW_LEN 20
#define MAX_CLIENTS 10

struct LoginInfo {
    char id[MAX_ID_LEN];
    char password[MAX_PW_LEN];
};

struct Message {
    char id[MAX_ID_LEN];
    char content[BUFSIZ];
};

int client_sockets[MAX_CLIENTS];  // 다중 클라이언트 소켓 배열
int client_count = 0;  // 현재 접속한 클라이언트 수
pid_t client_pids[MAX_CLIENTS];  // 클라이언트 프로세스 ID 배열
int pipe_fd[2]; // 파이프 디스크립터 정의

// 메시지를 모든 클라이언트에게 브로드캐스트하는 함수
void broadcast_message(struct Message *msg, int sender_sock) {
    for (int i = 0; i < client_count; i++) {    // 모든 클라이언트에 대해 반복
        if (client_sockets[i] != sender_sock) {     // 발신자를 제외한 모든 클라이언트에게 메시지 전송
            send(client_sockets[i], msg, sizeof(struct Message), 0);   // 클라이언트 소켓으로 메시지 전송
        }
    }
}

void remove_client(int csock) {
    for (int i = 0; i < client_count; i++) {    // 모든 클라이언트에 대해 반복
        if (client_sockets[i] == csock) {    // 제거할 클라이언트 소켓을 찾았을 때
            client_sockets[i] = client_sockets[client_count - 1];    // 마지막 클라이언트의 소켓을 현재 위치로 이동
            client_pids[i] = client_pids[client_count - 1];    // 마지막 클라이언트의 프로세스 ID를 현재 위치로 이동
            client_count--;    // 클라이언트 수 감소
            printf("클라이언트 제거됨. 현재 접속자 수: %d\n", client_count);
            break;
        }
    }
}

// 자식 프로세스 종료를 처리하는 시그널 핸들러
void sigchld_handler(int signo) {
    pid_t pid;
    int status;  // 자식 프로세스 상태 변수
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {    // 자식 프로세스가 종료되었을 때
        for (int i = 0; i < client_count; i++) {    // 모든 클라이언트에 대해 반복
            if (client_pids[i] == pid) {    // 종료된 자식 프로세스를 찾았을 때
                remove_client(client_sockets[i]);    // 클라이언트 제거
                break;
            }
        }
    }
}

void sigusr1_handler(int signo) { // 자식 프로세스가 메시지를 보내면 부모 프로세스에게 알리는 시그널 핸들러
    struct Message msg;
    read(pipe_fd[0], &msg, sizeof(msg));    // 파이프에서 메시지 읽기
    broadcast_message(&msg, -1);    // 모든 클라이언트에게 메시지 브로드캐스트
}

void daemonize() {
    pid_t pid, sid;

    // 부모 프로세스 종료
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // 새로운 세션 생성
    sid = setsid();
    if (sid < 0) {
        exit(EXIT_FAILURE);
    }

    // 작업 디렉토리 변경
    if ((chdir("/")) < 0) {
        exit(EXIT_FAILURE);
    }

    // 파일 권한 마스크 재설정
    umask(0);

    // 표준 파일 디스크립터를 /dev/null로 리다이렉션
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd != -1) {
        dup2(null_fd, STDIN_FILENO);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
        if (null_fd > 2) {
            close(null_fd);
        }
    }

    // syslog 열기
    openlog("chat_server", LOG_PID | LOG_NDELAY, LOG_DAEMON);
}

int main(int argc, char **argv)
{
    // 데몬화 과정 추가
    daemonize();

    // syslog를 사용하여 로그 남기기
    syslog(LOG_NOTICE, "채팅 서버 데몬이 시작되었습니다.");

    int ssock; // 서버 소켓 디스크립터  
    socklen_t clen; // 클라이언트 주소 길이
    pid_t pid; // 자식 프로세스 ID
    int n;  // 데이터 전송 및 수신 변수
    struct sockaddr_in servaddr, cliaddr; // 서버 및 클라이언트 주소 구조체
    char mesg[BUFSIZ];

    signal(SIGCHLD, sigchld_handler);   // 자식 프로세스 종료 시그널 핸들러
    signal(SIGUSR1, sigusr1_handler);   // 자식 프로세스가 메시지를 보내면 부모 프로세스에게 알리는 시그널 핸들러

    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0){    // 서버 소켓 생성
        perror("socket()");
        return -1;
    }

    if (pipe(pipe_fd) < 0) {    // 파이프 생성
        perror("pipe()");
        return -1;
    }

    // 주소 구조체에 주소 지정
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;  // 주소 체계 설정
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);  // 서버 IP 주소 설정
    servaddr.sin_port = htons(TCP_PORT); // 서버 포트 설정

    // bind 함수를 사용하여 서버 소켓의 주소 설정
    if (bind(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        perror("bind()");
        return -1;
    }

    // 동시에 접속하는 클라이언트의 처리를 위한 대기 큐를 설정(최대 8개 접속)
    if (listen(ssock, 8) < 0){
        perror("listen()");
        return -1;
    }

    // printf 대신 syslog 사용
    syslog(LOG_NOTICE, "서버가 시작되었습니다. 포트 %d", TCP_PORT);

    while (1) {
        clen = sizeof(cliaddr);  // 클라이언트 주소 길이 초기화
        int csock = accept(ssock, (struct sockaddr *)&cliaddr, &clen);  // 클라이언트 연결 accept
        if (csock < 0) {
            perror("accept()");
            continue;
        }

        if (client_count >= MAX_CLIENTS) {   // 접속 클라이언트 수가 최대 클라이언트 수에 도달했을 때
            printf("최대 클라이언트 수에 도달했습니다. 연결을 거부합니다.\n");
            close(csock);
            continue;
        }

        if ((pid = fork()) < 0) {
            perror("fork()");
        } else if (pid == 0) {    // 자식 프로세스
            close(ssock);    // 서버 소켓 닫기
            close(pipe_fd[0]); // 파이프 읽기 닫기

            // 로그인 정보 수신
            struct LoginInfo login;
            if (recv(csock, &login, sizeof(login), 0) <= 0) {
                perror("로그인 정보 수신 실패");
                exit(1);
            }

            // 패스워드와 관계 없이 무조건 로그인 성공
            strcpy(mesg, "로그인 성공");

            // 인증 결과 전송
            if (send(csock, mesg, strlen(mesg) + 1, 0) <= 0) {  // +1을 추가하여 null 종료 문자 포함
                perror("인증 결과 전송 실패");
                exit(1);
            }

            // printf 대신 syslog 사용
            syslog(LOG_NOTICE, "사용자 '%s' 로그인 성공", login.id);

            while (1) {
                struct Message msg;
                memset(&msg, 0, sizeof(msg));
                // 클라이언트로부터 메시지 읽기
                n = recv(csock, &msg, sizeof(msg), 0);  // 클라이언트로부터 메시지 수신
                if (n <= 0) {
                    if (n < 0 && errno == EINTR) continue;    // 수신 실패 시 다시 시도
                    perror("클라이언트로부터 recv() 실패");
                    break;
                }
                // printf 대신 syslog 사용
                syslog(LOG_NOTICE, "클라이언트로부터 받은 메시지: %s: %s", msg.id, msg.content);

                // 파이프를 통해 부모 프로세스에게 메시지 전달
                write(pipe_fd[1], &msg, sizeof(msg));  // 파이프에 메시지 쓰기
                kill(getppid(), SIGUSR1);  // getppid()를 사용하여 부모 프로세스에게 시그널 전달

                if (strcmp(msg.content, "q") == 0) {
                    // printf 대신 syslog 사용
                    syslog(LOG_NOTICE, "클라이언트 %s 종료", msg.id);
                    break;
                }
            } 

            close(pipe_fd[1]);
            close(csock);
            exit(0);
        } else {
            // 부모 프로세스
            client_sockets[client_count] = csock;  // 클라이언트 소켓을 배열에 저장
            client_pids[client_count] = pid;  // 클라이언트 프로세스 ID를 배열에 저장
            client_count++;  // 클라이언트 수 증가
            
            inet_ntop(AF_INET, &cliaddr.sin_addr, mesg, BUFSIZ);  // 클라이언트 IP 주소를 문자열로 변환
            // printf 대신 syslog 사용
            syslog(LOG_NOTICE, "클라이언트 연결됨: %s", mesg);  // 연결된 클라이언트의 IP 주소 출력
        }
    }

    close(pipe_fd[0]);
    close(pipe_fd[1]);
    close(ssock);
    closelog();
    return 0;
}
