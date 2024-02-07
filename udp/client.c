#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <pthread.h>

uint64_t get_cur_ns();
int compare_uint64(const void *a, const void *b);
void *tx_thread(void *arg);
void *rx_thread(void *arg);

// 전역변수
int TARGET_QPS, send_time, WRatio;

// 메시지 헤더 구조체 정의 
#pragma pack(1)
struct myheader {
    uint32_t op;      // 4바이트 unsigned int
    uint32_t key;     // 4바이트 unsigned int
    uint64_t value;   // 8바이트 unsigned long long
    uint64_t latency; // 8바이트 unsigned long long
	uint64_t time;
} __attribute__((packed));

struct sock_args {
	int sock;
	struct sockaddr_in srv_addr;
	int cli_addr_len;
};

/* Get current time in nanosecond-scale */
uint64_t get_cur_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t t = ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
  return t;
}

//비교 함수(qsort를 사용하여 99th-tail latency를 구하기 위함)
int compare_uint64(const void *a, const void *b) {
	return (*(uint64_t *)a - *(uint64_t *)b);
}

void *tx_thread(void *arg) {
	struct sock_args *args = (struct sock_args *)arg;
	uint64_t temp_time = get_cur_ns();
	struct myheader hdr;

	// GSL 난수 생성기 및 exponential 분포 설정
	const gsl_rng_type * T; // gsl_rng_type은 GSL에서 정의된 난수 생성기의 유형을 나타내는 데이터형식
							// 이 변수는 특정 유형의 난수 생성기를 선택하는 데 사용됨
	gsl_rng * r; // gsl_rng는 GSL에서 난수 생성기를 나타내는 구조체
				 // 이 변수는 실제로 난수 생성기의 인스턴스를 가리키게 됨
	gsl_rng_env_setup(); //GSL 환경 설정 - GSL 사용 전에 호출되어야 함
	T = gsl_rng_default; // GSL에서 기본적으로 제공하는 난수 생성기의 유형을 T에 할당
	r = gsl_rng_alloc(T); // 선택한 난수 생성기 유형(T)를 기반으로 난수 생성기의 인스턴스를 할당
						  //'r'은 난수 생성기를 조작하기 위한 핸들을 가리키게 됨

	// exponential 분포의 파라미터인 람다를 설정
	// TARGET_QPS는 초당 전송되어야 하는 요청의 개수
	// 1e - 9는 1을 10^9로 나누어 초당 요청 수를 계산하는데 사용됨
	double lambda = TARGET_QPS * 1e-9; // TARGET_QPS가 인자로 받은 target tx rate임

	// exponential 분포의 다른 표현인 뮤를 계산. 
	// 뮤는 평균 inter-arrival 시간의 역수로, 여기서는 초당 요청 수의 역수로 계산됨
	double mu = 1.0 / lambda;

	int total_requests = TARGET_QPS * send_time;
	/* 메인 Tx 루프 시작 */
	for (int i = 0; i < total_requests; ++i) {
		/*Packet inter-arrival time 을 Exponentional하게 보내기 위한 연산 과정들이다 */
		uint64_t inter_arrival_time = (uint64_t)(gsl_ran_exponential(r, mu));
		temp_time += inter_arrival_time;

		// Inter-inter_arrival_time만큼 시간이 지나지 않았다면 무한루프를 돌며 대기한다.
		while (get_cur_ns() < temp_time); 

		// 여기에 패킷 보내는 코드 추가
		hdr.key = rand() % 100000;
		if (rand() % 100 < WRatio) { //쓰기이면
			hdr.op = 1;
			hdr.value = rand() % 100000;
		} 
		else hdr.op = 0; // 읽기이면
		
		hdr.time = get_cur_ns();
		sendto(args->sock, &hdr, sizeof(struct myheader), 0, (struct sockaddr *)&(args->srv_addr), sizeof(args->srv_addr));
	}
	return NULL;
}

void *rx_thread(void *arg) {
	struct sock_args *args = (struct sock_args *)arg;
	// 답신의 latency를 저장할 배열
	uint64_t latencies[send_time * TARGET_QPS];

	bool continue_processing = true;
	while (continue_processing) {
		struct myheader reply_hdr;
		for (int i = 0; i < send_time * TARGET_QPS; i++) {
			recvfrom(args->sock, &reply_hdr, sizeof(struct myheader), 0, (struct sockaddr *)&(args->srv_addr), &(args->cli_addr_len));

			latencies[i] = get_cur_ns() - reply_hdr.time;
			printf("%d 번째 요청 수신\n", i + 1);
		}

		// latency 정렬
		qsort(latencies, send_time * TARGET_QPS, sizeof(uint64_t), compare_uint64);

		// 평균 latency 계산
		uint64_t total_latency = 0;
		for (int i = 0; i < send_time * TARGET_QPS; i++) 
			total_latency += latencies[i];
		double avg_latency = (double)total_latency / send_time * TARGET_QPS;

		// 결과 출력
		printf("Average latency: %.2lf nanoseconds\n", avg_latency);
		printf("99th percentile latency: %lu nanoseconds\n", latencies[(int)(0.99 * send_time * TARGET_QPS)]);
	
		continue_processing = false;
	}

	return NULL;
}



int main(int argc, char *argv[]) {
	srand(time(NULL));
	if ( argc != 4 ){
		printf("Usage: %s <target Tx rate> <sending time> <write ratio>\n", argv[0]);
		return 1;
	}

	TARGET_QPS = atoi(argv[1]);
	send_time = atoi(argv[2]);
	WRatio = atoi(argv[3]);
	
	/* localhost에서 통신할 것이므로 서버 ip주소도 그냥 localhost */
	const char* server_name = "localhost"; // 127.0.0.1
	int SERVER_PORT = 5001;

	struct sockaddr_in srv_addr; // Create socket structure
	memset(&srv_addr, 0, sizeof(srv_addr)); // Initialize memory space with zeros
	srv_addr.sin_family = AF_INET; // IPv4
	srv_addr.sin_port = htons(SERVER_PORT);
	inet_pton(AF_INET, server_name, &srv_addr.sin_addr);  // Convert IP addr. to binary

	int sock;
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("Could not create socket\n");
		exit(1);
	}

	struct sockaddr_in cli_addr;
	int cli_addr_len = sizeof(cli_addr);

	struct sock_args args;
	args.sock = sock;
	args.srv_addr = srv_addr;
	args.cli_addr_len = cli_addr_len;

	pthread_t tx, rx;
	pthread_create(&tx, NULL, tx_thread, &args);
	pthread_create(&rx, NULL, rx_thread, &args);
	pthread_join(tx, NULL);
	pthread_join(rx, NULL);


	close(sock);
	return 0;
}
