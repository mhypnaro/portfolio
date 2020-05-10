#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include<readline/readline.h>
#include<readline/history.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace std;

int session (int connSocket) {
    // used for command processing
    char* command;
    char* response = (char*) calloc(1024, sizeof(char));
    int amountRead;
    bool contentLeft;

    // keep running until the user enters "exit"
    while(1) {
        amountRead = 0;
        contentLeft = true;
        command = readline("client $ ");
        if (strcmp(command, "") == 0) {
            continue;
        }
        
        // if no command was entered
        if (command == NULL) {
            printf("Invalid command.\n");
            free(command);
            continue;
        
        // if the user wants to end the session
        } else if (strcmp(command, "exit") == 0) {
            if (write(connSocket, command, strlen(command)) == -1) {
                printf("ERROR: (session) (write) (%d): %s\n", errno, strerror(errno));
                free(command);
                free(response);
                return -1;
            } else {
                free(command);
                free(response);
                break;
            }
        }

        // send the message
        if (write(connSocket, command, strlen(command)) == -1) {
            printf("ERROR: (session) (write) (%d): %s\n", errno, strerror(errno));
            free(command);
            free(response);
            return -1;
        }
        free(command);

        // read the response and write it to stdout
        while(1) {
            memset(response, '\0', 1024);
            amountRead = read(connSocket, response, 1024);

            if (strcmp(response, "done sending") == 0) {
                break;
            }

            // if something went wrong
            if (amountRead == -1) {
                printf("ERROR: (session) (read) (%d): %s\n", errno, strerror(errno));
                free(response);
                return -1;
            }

            // if we have more left to read
            if (amountRead == 1024) {
                if (write(1, response, 1024) == -1) {
                    printf("ERROR: (session) (write) (%d): %s\n", errno, strerror(errno));
                    free(response);
                    return -1; 
                }
            }

            // if we are finished reading
            if (amountRead < 1024) {
                if (write(1, response, amountRead) == -1) {
                    printf("ERROR: (session) (write) (%d): %s\n", errno, strerror(errno));
                    free(response);
                    return -1; 
                }
            }
        }
    }
    return 0;
}


int main(int argc, char* argv[]) {
	// validate the number of arugments
	if (argc != 3) {
		printf("FATAL ERROR (input): Invalid number of arguments. See README for details.\n");
		exit(EXIT_FAILURE);
	}

	// read in the arguments
    char* serverIP = argv[1];
    int port = atoi(argv[2]);
    if (port == 0) {
        printf("FATAL ERROR (input): Invalid port number.\n");
        exit(EXIT_FAILURE);
    }

    // use the arguemnts to create an addr struct
    struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family=AF_INET;
	addr.sin_port=htons(port);

    // create the socket
    int connSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (connSocket == -1) {
        printf("FATAL ERROR: (socket) (%d): %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // converts the passed in IP to network format
    if (inet_pton(AF_INET, serverIP, &(addr.sin_addr)) == -1) {
		printf("FATAL ERROR: (inet_pton) (%d): %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// attempt to connect to the server
	if (connect(connSocket, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		printf("FATAL ERROR: (connect) (%d): %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
	}

    // run the simulated shell
    int sessionReturn = session(connSocket);
    close(connSocket);
    if (sessionReturn == 0) {
        exit(EXIT_SUCCESS);
    } else {
        printf("Process terminated by error.\n");
        exit(EXIT_FAILURE);
    }
}
