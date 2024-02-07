#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <hiredis/hiredis.h>
#include <time.h>

//메시지 헤더 구조체 정의
#pragma pack(1)
struct myheader_hdr {
    uint32_t op;      // 4바이트 unsigned int
    uint32_t key;     // 4바이트 unsigned int
    uint64_t value;   // 8바이트 unsigned long long
    uint64_t latency; // 8바이트 unsigned long long
} __attribute__((packed));

/* Get current time in nanosecond-scale */
uint64_t get_cur_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t t = ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
  return t;
}

//put 함수 정의
int put(redisContext *c,  uint32_t key, uint64_t value) {
    redisReply *reply = redisCommand(c, "SET %u %lu", key, value);
    if (reply == NULL) {
        printf("PUT 작업 중 오류 발생: %s\n", c->errstr);
        return -1;
    }
    freeReplyObject(reply);
    return 0;
}

//get 함수 정의
char* get(redisContext *c, uint32_t key) {
    redisReply *reply = redisCommand(c, "GET %u", key);
    uint64_t value;

    if (reply->str == NULL) 
        value = strdup("null");
    else
        value = strdup(reply->str);

    freeReplyObject(reply);
    return value;
}

void generateKeyValues(redisContext *c, int random_key_size) {
    for (int i = 0; i < random_key_size; i++) {
        put(c, rand() % 100000, rand() % 100000); 
    }
}



//main 함수 시작
int main(int argc, char *argv[]) {
	srand(time(NULL));
	// Connect to Redis server
    redisContext *redis_context = redisConnect("127.0.0.1", 6379);
    if (redis_context->err) {
        printf("Failed to connect to Redis: %s\n", redis_context->errstr);
        return 1;
    }

	if ( argc < 2 ){
	 printf("Input : %s port number\n", argv[0]);
	 return 1;
	}

	int SERVER_PORT = atoi(argv[1]);

	struct sockaddr_in srv_addr;
	memset(&srv_addr, 0, sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(SERVER_PORT);
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	int sock; 
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("Could not create listen socket\n");
		exit(1);
	}

	if ((bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr))) < 0) {
		printf("Could not bind socket\n");
		exit(1);
	}

	struct sockaddr_in cli_addr;
  	int cli_addr_len = sizeof(cli_addr);

	int n = 0;
	struct myheader_hdr RecvBuffer;
  	struct myheader_hdr SendBuffer;

	// 서버 실행 시 임의의 키-값 데이터를 생성한다
	// 키 개수: 10만개, 키 범위: 0~99999
	int random_key_size = 100000;
	generateKeyValues(redis_context, random_key_size);
	

	while (1) {
		//recvfrom은 udp 소켓에서 데이터 수신에 사용됨
		//데이터를 수신하지 못한 경우 -1 반환, 성공한 경우 수신한 바이트 수 반환
		n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), 0, (struct sockaddr *)&cli_addr, &cli_addr_len);
		if (n > 0) {
			uint64_t value;

			// SendBuffer op, key, value를 클라이언트로부터 받은 것과 동일하게 초기화
			SendBuffer.op = RecvBuffer.op;
			SendBuffer.key = RecvBuffer.key;
			SendBuffer.value = RecvBuffer.value;

			if (RecvBuffer.op) { // 쓰기이면
				put(redis_context, RecvBuffer.key, RecvBuffer.value);
			} else {// 읽기이면
				value = get(redis_context, RecvBuffer.key);
				SendBuffer.value = value;
				free(value);
			}
        } 
		SendBuffer.latency = get_cur_ns();
		sendto(sock, &SendBuffer, sizeof(SendBuffer), 0, (struct sockaddr *)&cli_addr, sizeof(cli_addr));
	}
	close(sock);

    redisFree(redis_context); // 메인 함수 마지막에 넣어준다. return 0; 하기 직전에.

	return 0;
}