int main(int argc, char* argv[]) {

	int port = atoi(argv[1]);
	if (port == 0) {
		printf("FATAL ERROR (input): Invalid port number.\n");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in server;
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	
	if (inet_pton(AF_INET, argv[2], &(server.sin_addr)) == -1) {
		printf("FATAL ERROR (inet_pton) (%d): %s\n", errno, strerror(errno));
	}

	int connSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (connSocket == -1) {
		printf("FATAL ERROR (socket) (%d): %s\n", errno, strerror(errno));
	}
	
	if (connect(connSocket, (struct sockaddr*)&server, sizeof(server)) == -1) {
		printf("FATAL ERROR (socket) (%d): %s\n", errno, strerror(errno));
	}

