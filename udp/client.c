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
void read_latency_from_file(uint64_t *latencies, const char *filename);
void write_latency_to_file(uint64_t latency, const char *filename);

// 전역변수
int TARGET_QPS, send_time, WRatio;
int VALUE_SIZE = 128;
uint64_t all_latencies[MAX_LATENCIES]; // 각 스레드의 레이턴시를 저장하는 배열
int all_latency_count = 0; // 모든 스레드의 레이턴시 개수를 추적하는 변수
int MAX_FILENAME_LENGTH = 256;
int MAX_LATENCIES = 10000000;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// 메시지 헤더 구조체 정의 
#pragma pack(1)
struct myheader {
    bool isRead = true; // 쓰기보다 읽기 요청이 더 많으므로 read일 때를 true로 설정
    uint64_t key;     // 4바이트 unsigned int
    char value[VALUE_SIZE];  // 128바이트 문자열
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

// 파일에 latency 기록
void write_latency_to_file(uint64_t latency, const char *filename) {
    FILE *fp = fopen(filename, "a"); // "a" 모드를 사용하여 파일 끝에 이어쓰기 모드로 열기
    if (fp == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    fprintf(fp, "%lu\n", latency);
    fclose(fp);
}

// 파일에서 latency를 읽어오고 정렬하여 배열에 저장
void read_latency_from_file(uint64_t *latencies, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

	int i = 0;
    while (fscanf(fp, "%lu", &latencies[i]) != EOF) {
    	all_latency_count++;
		i++;
    }

    fclose(fp);
}

// 랜덤 문자열(value)을 생성하는 함수
void generate_random_value(char *value, size_t length) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    srand((unsigned int)time(NULL));
    for (size_t i = 0; i < length - 1; ++i) {
        int index = rand() % (int)(sizeof(charset) - 1);
        value[i] = charset[index];
    }
    value[length - 1] = '\0'; // 문자열 종료를 나타내는 널 문자 추가
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
		hdr.key = rand() % 1000000;
		if (rand() % 100 < WRatio) { //쓰기이면
			hdr.isRead = 0;
			hdr.value = generate_random_value(hdr.value, VALUE_SIZE);
		}
		
		hdr.time = get_cur_ns();
		sendto(args->sock, &hdr, sizeof(struct myheader), 0, (struct sockaddr *)&(args->srv_addr), sizeof(args->srv_addr));
	}
	return NULL;
}


void *rx_thread(void *arg) {
	struct sock_args *args = (struct sock_args *)arg;
	// 답신의 latency를 저장할 배열
	//uint64_t latencies[c];
	char filename[MAX_FILENAME_LENGTH];

	// 각 스레드마다 다른 파일 이름 생성
	snprintf(filename, MAX_FILENAME_LENGTH, "latency_%lu.txt", pthread_self()); 

	bool continue_processing = true;
	while (continue_processing) {
		struct myheader reply_hdr;
		for (int i = 0; i < send_time * TARGET_QPS; i++) {
			recvfrom(args->sock, &reply_hdr, sizeof(struct myheader), 0, (struct sockaddr *)&(args->srv_addr), &(args->cli_addr_len));

			uint64_t latency = get_cur_ns() - reply_hdr.time;

			// 파일에 latency 기록
			write_latency_to_file(latency, filename);

			printf("%d 번째 요청 수신\n", i + 1);
		}
		
        pthread_mutex_lock(&mutex);
		read_latency_from_file(all_latencies, filename);
		pthread_mutex_unlock(&mutex);

		continue_processing = false;
	}

	// 평균 latency 계산
	// uint64_t total_latency = 0;
	// for (int i = 0; i < latency_count; i++) 
	// 	total_latency += latencies[i];
	// double avg_latency = (double)total_latency / latency_count;

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

	 // 모든 스레드가 종료되면 모든 레이턴시를 정렬
    qsort(all_latencies, all_latency_count, sizeof(uint64_t), compare_uint64);

    // 결과 출력
    printf("50th percentile latency: %lu nanoseconds\n", all_latencies[(int)(0.5 * all_latency_count)]);
    printf("99th percentile latency: %lu nanoseconds\n", all_latencies[(int)(0.99 * all_latency_count)]);

	close(sock);
	
    // mutex 해제
    pthread_mutex_destroy(&mutex);

	return 0;
}
