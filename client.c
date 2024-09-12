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
#define MAX_MESSAGES 100
#define SCREEN_WIDTH 80

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
pid_t pid;
struct LoginInfo login;

struct Message message_history[MAX_MESSAGES];  // 채팅방을 만들기 위한 히스토리를 저장할 배열
int message_count = 0;  // 채팅 히스토리의 개수를 저장할 변수

void clear_screen() {
    printf("\033[2J\033[H");    //ANSI 이스케이프 코드를 사용하여 화면을 지우는 함수
}

void print_line() {
    for (int i = 0; i < SCREEN_WIDTH; i++) {
        printf("-");
    }
    printf("\n");
}

void print_centered(const char* text) {
    int padding = (SCREEN_WIDTH - strlen(text)) / 2;  // 텍스트를 가운데 정렬하기 위한 공백 계산
    for (int i = 0; i < padding; i++) { 
        printf(" "); 
    }
    printf("%s\n", text);  // 텍스트를 가운데 정렬하여 출력
}

void update_chat_screen() {
    clear_screen();  // 화면을 지우는 함수 호출
    print_line();
    print_centered("채팅방");
    print_centered("(종료:/q) (검색:/s)");
    printf("   your id: %s\n", login.id);
    print_line();

    int start = (message_count > MAX_MESSAGES) ? message_count - MAX_MESSAGES : 0;  // 채팅 히스토리 출력을 위한 시작 인덱스 계산
    for (int i = start; i < message_count; i++) {
        printf("[%s]: %s\n", message_history[i].id, message_history[i].content);  // 채팅 히스토리 출력
    }

    print_line();
    printf("메시지를 입력하세요 : ");
    fflush(stdout);  // 출력 버퍼를 비우는 함수 호출
}
// 메시지를 채팅 히스토리에 추가하는 함수
void add_message(const char* id, const char* content) { 
    if (message_count < MAX_MESSAGES) {  // 채팅 히스토리의 개수가 최대 개수보다 작을 때
        strcpy(message_history[message_count].id, id);  // 메시지 ID를 복사
        strcpy(message_history[message_count].content, content);  // 메시지 내용을 복사
        message_count++;  // 메시지 개수 증가
    } else {  // 채팅 히스토리의 개수가 최대 개수보다 클 때
        for (int i = 0; i < MAX_MESSAGES - 1; i++) {  // 모든 메시지를 한 칸씩 앞으로 이동
            message_history[i] = message_history[i + 1];
        }
        strcpy(message_history[MAX_MESSAGES - 1].id, id);  // 새 메시지 ID를 마지막 위치에 복사
        strcpy(message_history[MAX_MESSAGES - 1].content, content);  // 새 메시지 내용을 마지막 위치에 복사
    }
    update_chat_screen();  // 채팅 화면 업데이트
}

void sigint_handler(int signo) {    // SIGINT 시그널 핸들러 함수 정의
    close(ssock);                   // 서버 소켓 연결 종료
    close(pipe_fd[0]);              // 파이프 읽기 종단 닫기
    close(pipe_fd[1]);              // 파이프 쓰기 종단 닫기
    if (pid > 0) {                  // 자식 프로세스가 존재하는 경우
        kill(pid, SIGTERM);         // 자식 프로세스에 종료 시그널 보내기
    }
    exit(0);
}

void search_messages() {    // 메시지를 검색하는 함수
    char keyword[BUFSIZ];   // 검색할 키워드를 저장할 문자열 배열 선언
    printf("검색할 키워드를 입력하세요: ");
    fgets(keyword, BUFSIZ, stdin);
    keyword[strcspn(keyword, "\n")] = 0;  // 개행 문자 제거

    clear_screen();
    print_line();
    print_centered("검색 결과");
    print_line();

    int found = 0;  // 검색된 메시지 개수를 저장할 변수 초기화
    for (int i = 0; i < message_count; i++) {
        if (strstr(message_history[i].content, keyword) != NULL) {  // 메시지 내용에 키워드가 포함되어 있는지 확인
            printf("[%s]: %s\n", message_history[i].id, message_history[i].content);  // 검색된 메시지 출력
            found++;  // 검색된 메시지 개수 증가
        }
    }

    if (found == 0) {  // 검색된 메시지가 없을 때
        printf("검색 결과가 없습니다.\n");
    }

    print_line();
    printf("아무 키나 눌러 채팅방으로 돌아가기...");
    getchar();
    update_chat_screen();
}

int main(int argc, char **argv) {
    struct sockaddr_in servaddr;
    char mesg[BUFSIZ];
    
   
    if (argc < 2) {
        printf("Usage : %s IP_ADDRESS\n", argv[0]);
        return -1;
    }

    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {  // 소켓 생성
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

    // 로그인 화면 출력(패스워드와 상관없이 로그인 성공)
    clear_screen();
    print_line();
    print_centered("로그인");
    print_line();

    printf("아이디를 입력하세요: ");
    fgets(login.id, MAX_ID_LEN, stdin);
    login.id[strcspn(login.id, "\n")] = 0;

    printf("비밀번호를 입력하세요: ");
    fgets(login.password, MAX_PW_LEN, stdin);
    login.password[strcspn(login.password, "\n")] = 0;

    if (send(ssock, &login, sizeof(login), 0) <= 0) { // 로그인 정보 서버로 전송
        perror("send()");
        return -1;
    }

    memset(mesg, 0, BUFSIZ);
    if (recv(ssock, mesg, BUFSIZ, 0) <= 0) {  // 서버로부터 로그인 결과 수신
        perror("recv()");
        return -1;
    }

    if (strcmp(mesg, "로그인 성공") != 0) {  // 로그인 성공이 반환이 안되면 실패 출력
        printf("로그인 실패: %s\n", mesg);
        close(ssock);
        return -1;
    }

    if (pipe(pipe_fd) < 0) {  // 파이프 생성
        perror("pipe()");
        return -1;
    }

    signal(SIGINT, sigint_handler);  // SIGINT 시그널 핸들러 등록

    pid = fork();

    if (pid < 0) {
        perror("fork()");
        return -1;
    } else if (pid == 0) {
        // 자식 프로세스: 메시지 수신
        close(pipe_fd[1]);  // 쓰기 파이프 닫기
        while (1) {
            struct Message received_msg;
            memset(&received_msg, 0, sizeof(received_msg));  // 메시지 구조체 초기화
            if (recv(ssock, &received_msg, sizeof(received_msg), 0) <= 0) {  // 서버로부터 메시지 수신
                perror("recv()");
                break;
            }
            add_message(received_msg.id, received_msg.content);  // 채팅 히스토리에 메시지 추가
            write(pipe_fd[0], "1", 1);  // 부모에게 메시지 수신 알림
        }
        close(pipe_fd[0]);
        exit(0);
    } else {
        // 부모 프로세스: 메시지 송신
        close(pipe_fd[0]);  // 읽기 파이프 닫기
        update_chat_screen();  // 채팅 화면 업데이트
        while (1) {
            struct Message msg;
            strcpy(msg.id, login.id);
            
            fgets(msg.content, BUFSIZ, stdin);
            msg.content[strcspn(msg.content, "\n")] = 0;  // 개행 문자 제거

            if (strcmp(msg.content, "/q") == 0) {
                printf("채팅을 종료합니다.\n");
                strcpy(msg.content, "q");  // 서버에게 종료 신호 전송
                send(ssock, &msg, sizeof(msg), 0);  // 종료 메시지 전송
                break;
            } else if (strcmp(msg.content, "/s") == 0) {
                search_messages();
                continue;
            }

            add_message(msg.id, msg.content);  // 채팅 히스토리에 메시지 추가

            if (send(ssock, &msg, sizeof(msg), 0) <= 0) {  // 서버 소켓으로 메시지 전송
                perror("send()");
                break;
            }

            // 자식 프로세스의 메시지 수신 대기
            char buf[2];
            read(pipe_fd[1], buf, 1);  // 자식 프로세스의 메시지 수신 대기
        }
        close(pipe_fd[1]);
    }

    close(ssock);
    kill(pid, SIGTERM);
    wait(NULL);
    return 0;
}

