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

#include <string>
#include <vector>

using namespace std;


int session (int sessionSocket, int sessionPID) {
    char* command = (char*) calloc(1024, sizeof(char));
    char* response = (char*) calloc(1024, sizeof(char));
    string finished = "done sending";
    int amountRead;
    bool contentLeft;
    FILE *fp;

    while(1) {
        contentLeft = true;
        // read in the client's command
        amountRead = read(sessionSocket, command, 1024);
        if (amountRead == -1) {
            printf("ERROR (Process %d) (read from socket) (%d): %s\n", sessionPID, errno, strerror(errno));
            free(response);
            free(command);
            return -1;
        }

        // if the client ^C'd out of their process
        if (amountRead == 0) {
            printf("ERROR (Process %d): Received EOF. Client probably ^C'd.\n", sessionPID);
            free(response);
            free(command);
            return -1;
        }

        // check if the client wants to close the connection
        if (strcmp(command, "exit") == 0) {
            free(response);
            free(command);
            return 0;
        }

        // run the program
        fp = popen(command, "r");
        if (fp == NULL) {
            printf("ERROR (Process %d) (popen) (%d): %s\n", sessionPID, errno, strerror(errno));
            memset(command, '\0', 1024);
            continue;
        }

        // while there is still data to read from the pipe
        while(contentLeft == true) {

            // clear the buffer and read from the pipe
            memset(response, '\0', 1024);
            amountRead = fread(response, sizeof(char), 1024, fp);

            // if we read nothing
            if (amountRead == 0) {
                break;
            }

            // if we could not read from the pipe
            if (amountRead == -1) {
                printf("ERROR (Process %d) (read from fp) (%d): %s\n", sessionPID, errno, strerror(errno));
                free(response);
                free(command);
                return -1;
            }

            // if we read enough to fill the whole buffer
            if (amountRead == 1024) {
                if (write(sessionSocket, response, 1024) == -1) {
                    printf("ERROR (Process %d) (write to socket) (%d): %s\n", sessionPID, errno, strerror(errno));
                    free(response);
                    free(command);
                    return -1;
                }

            // if we don't read enough to fill the whole buffer
            } else {
                if (write(sessionSocket, response, amountRead) == -1) {
                    printf("ERROR (Process %d) (write to socket) (%d): %s\n", sessionPID, errno, strerror(errno));
                    free(response);
                    free(command);
                    return -1;
                }
            }
        }

        /*
        Sleep for a brief moment to ensure that our "done sending" message does not get
        mixed in with the actual response to the command
        */
        usleep(250000);

        // tell the other side that we are finished
        if (write(sessionSocket, finished.c_str(), finished.size()) == -1) {
            printf("ERROR (Process %d) (write to socket) (%d): %s\n", sessionPID, errno, strerror(errno));
            free(response);
            free(command);
            return -1;
        }

        // close the pipe and reset the buffer which will read in the command
        fclose(fp);
        memset(command, '\0', 1024);
    }
}


int main(int argc, char* argv[]) {
    // validate input
	if (argc != 2) {
		printf("FATAL ERROR (input): Invalid number of arguments. See README for details.\n");
		exit(1);
	}

	int port = atoi(argv[1]);
	if (port == 0) {
		printf("FATAL ERROR (input): Invalid arguments. See README for details.\n");
		printf("Did you mean to specify a port number?\n");
	}

	// intializing the address and port
	struct sockaddr_in addr;
	bzero(&addr, sizeof addr);
	addr.sin_family=AF_INET;
	addr.sin_port=htons(port);
	
	// create socket to listen for connections on
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd == -1) {
		printf("FATAL ERROR (socket) (%d): %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	// bind to a port and listen for incoming connections
	if (bind(listenfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
		printf("FATAL ERROR (bind) (%d): %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (listen(listenfd, 3) == -1) {
		printf("FATAL ERROR (listen) (%d): %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Server
	while(1) {
		// acquire a connection
		int newConn = accept(listenfd, (struct sockaddr*) NULL, NULL);

        // child -- wait for new commands and terminate in case of a failure or a success
        if (fork() == 0) {
            int pid = getpid();
            int sessionReturn = session(newConn, pid);
            close(newConn);
            if (sessionReturn == 0) {
                exit(EXIT_SUCCESS);
            } else {
                exit(EXIT_FAILURE);
            }

        } 
        
        // parent
        else {
            close(newConn);
        }
    }
    return 0;
}
