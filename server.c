#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
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

	int maxlen = 1024;
	int n = 0;
	char RecvBuffer[maxlen];
  	char SendBuffer[maxlen];

	while (1) {
		//recvfrom은 udp 소켓에서 데이터 수신에 사용됨
		//데이터를 수신하지 못한 경우 -1 반환, 성공한 경우 수신한 바이트 수 반환
		n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), 0, (struct sockaddr *)&cli_addr, &cli_addr_len);
		if (n > 0) {
			RecvBuffer[n] = '\0'; // Null-terminate the received string

			if (strncmp(RecvBuffer, "get", 3) == 0) {
			 	sprintf(SendBuffer, "the value for %c is 0\n", RecvBuffer[4]);
			 	//printf("Sending to client: %s\n", SendBuffer);
            }
			else if (strncmp(RecvBuffer, "put", 3) == 0) {
			 	sprintf(SendBuffer, "your put for %c=%c is done!\n", RecvBuffer[4], RecvBuffer[6]);
			 	//printf("Sending to client: %s\n", SendBuffer);
            }
			else {
				sprintf(SendBuffer, "\n");
			}

			sendto(sock, &SendBuffer, sizeof(SendBuffer), 0, (struct sockaddr *)&cli_addr, sizeof(cli_addr));
		}
	}
	close(sock);

	return 0;
}
