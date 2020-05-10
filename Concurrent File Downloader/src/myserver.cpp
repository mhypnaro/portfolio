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

/*
Purpose: The server checks what step of the protocol that the
	client is trying to make.
Parameters:
	socket: The socket that the connecton has been made on.
Returns:
	1: Check-In step
	2: File size step
	3: Download step
*/
int checkStepType(int socket) {

	char* msg = (char*) calloc(3, sizeof(char));
	read(socket, msg, 2);
	// checkin step
	if ( strcmp (msg, "Do") == 0) {
		free(msg);
		return 1;
	
	// size step
	} else if ( strcmp (msg, "Fi") == 0) {
		free(msg);
		return 2;
	
	// download step
	} else if ( strcmp (msg, "Se") == 0) {
		free(msg);
		return 3;
	
	// unexpected message
	} else {
		free(msg);
		return -1;
	}
}


/*
Corresponds to the server side Check-In step.
Purpose: Checks if the server can open the file and tells the client.
Parameters:
	socket: The socket that the connecton has been made on.
*/
void tryOpen(int socket) {
	// read in the client's message
	// note that the first two characters of the stream have already been read by checkStepType()
	char* msg = (char*) calloc(1024, sizeof(char));
	read(socket, msg, 1024);
	
	// extract the filename from the client's message
	string msgStr = string(msg);
	int endPos = msgStr.find("?");
	int startPos = msgStr.find(": ") + 2;
	string fileName = msgStr.substr(startPos, endPos-startPos);
	free(msg);

	// form a response based on whether or not the file can be opened
	string response;
	FILE* fd = fopen(fileName.c_str(), "r");
	if (fd == NULL) {
		response = "N";
	} else {
		response = "Y";
		fclose(fd);
	}

	// send the response
	if (write(socket, response.c_str(), response.size()) == - 1) {
		printf("ERROR: (tryopen) (write) (%d): %s\n", errno, strerror(errno));
	}
}


/*
Corresponds to the server side File size step
Purpose: Determines the size of the file requested by the client and tells the client.
Parameters:
	socket: The socket that the connecton has been made on.
*/
void getFileSize(int socket) {
	// read in the client's message
	// note that the first two characters of the stream have already been read by checkStepType()
	char* msg = (char*) calloc(1024, sizeof(char));
	read(socket, msg, 1024);
	
	// extract the filename from the client's message
	string msgStr = string(msg);
	int endPos = msgStr.find("?");
	int startPos = msgStr.find(": ") + 2;
	string fileName = msgStr.substr(startPos, endPos-startPos);
	free(msg);

	/*
	We don't need to perform error handling here since we already know that the server
		can open the file specified as a result of the Check-In step (tryOpen)
	We use the "stat" utility to get the filesize in bytes.
	*/
	struct stat fileStat;
	stat(fileName.c_str(), &fileStat);
	int fileSize = fileStat.st_size;
	
	// form a response to send to the client
	string response;
	response = to_string(fileSize);

	// send the response
	if (write(socket, response.c_str(), response.size()) == - 1) {
		printf("ERROR: (tryopen) (write) (%d): %s\n", errno, strerror(errno));
	}
}


/*
Corresponds to the server side Download step.
Purpose: Sends a specific amount of bytes from the file specified at the offset specified
Parameters:
	socket: The socket that the connecton has been made on.
*/
void sendChunk(int socket) {
	// read in the client's message and prepare some variables
	char* msg = (char*) calloc(1024, sizeof(char));
	read(socket, msg, 1024);
	int endPos;
	int startPos;
	string msgStr = string(msg);
	
	// how much to send
	endPos = msgStr.find(" bytes");
	startPos = msgStr.find("nd: ") + 4;
	int amountToSend = stoi(msgStr.substr(startPos, endPos-startPos));

	// what file to read from
	endPos = msgStr.find(" starting");
	startPos = msgStr.find("of: ") + 4;
	string fileName = msgStr.substr(startPos, endPos-startPos);

	// where to start reading from the file
	endPos = msgStr.find(".");
	startPos = msgStr.find("from: ") + 6;
	int startLoc = stoi(msgStr.substr(startPos, endPos-startPos));

	// keep track of how much left we have to read and write
	int amountRead;
	int amountWritten;
	int offset = startLoc;
	bool contentLeft = true;
	
	// the buffer we will read to and write from
	char* data = (char*) calloc (1024, sizeof(char));

	/* 
	Open the file
	Remember, we don't need to error check here since we know we can open the file already as a
		result of performing the Check-In step (tryOpen)
	*/
	int file = open(fileName.c_str(), O_RDONLY);

	/*
	While there is still something left to read...
	If there is an error, break out of the loop, causing the server to close the socket prematurely and let
		the client fix the issue.
	*/
	while (contentLeft == true) {

		// if the amont left to read can't fit in the buffer
		if (amountToSend > 1024) {
			// read 1024 bytes starting from offset and write to socket
			amountRead = pread(file, data, 1024, offset);
			if (amountRead == -1) {
				printf("ERROR: (sendChunk) (read) (%d): %s\n", errno, strerror(errno));
				break;
			}
			amountWritten = write(socket, data, amountRead);
			if (amountRead == -1) {
				printf("ERROR: (sendChunk) (write) (%d): %s\n", errno, strerror(errno));
				break;
			}

			// change how much we have left to send and from what offset
			offset += amountRead;
			amountToSend -= amountRead;
			memset(data, 0, 1024);

		// if the amount left to read can fit fully in the buffer
		} else if (amountToSend <= 1024) {

			// read however many bytes are left and write to the socket
			amountRead = pread(file, data, amountToSend, offset);
			if (amountRead == -1) {
				printf("ERROR: (sendChunk) (read) (%d): %s\n", errno, strerror(errno));
				break;
			}
			amountWritten = write(socket, data, amountRead);
			if (amountRead == -1) {
				printf("ERROR: (sendChunk) (write) (%d): %s\n", errno, strerror(errno));
				break;
			}

			// change how much we have left to send and from what offset
			offset += amountRead;
			amountToSend -= amountRead;
			memset(data, 0, 1024);

			// if we have read all we were supposed to with this server, exit the loop
			if (amountToSend == 0) {
				contentLeft = false;
			}
		}
	}
	close (file);
	free (data);
}

int main(int argc, char* argv[]) {
	// get the port from the input
	if (argc != 2) {
		printf("FATAL ERROR (input): Invalid number of arguments. See README for details.\n");
		exit(1);
	}
	int port = atoi(argv[1]);
	if (port == 0) {
		printf("FATAL ERROR (input): Invalid arguments. See README for details.\n");
		printf("Did you mean to specify a port number?\n");
	}
	
	// create socket to listen for connections on
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd == -1) {
		printf("FATAL ERROR (socket) (%d): %s\n", errno, strerror(errno));
		exit(1);
	}
	
	// intializing the address and port
	struct sockaddr_in addr;
	bzero(&addr, sizeof addr);
	addr.sin_family=AF_INET;
	addr.sin_port=htons(port);
	
	// bind to a port and listen for incoming connections
	if (bind(listenfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
		printf("FATAL ERROR (bind) (%d): %s\n", errno, strerror(errno));
		exit(1);
	}
	if (listen(listenfd, 3) == -1) {
		printf("FATAL ERROR (listen) (%d): %s\n", errno, strerror(errno));
		exit(1);
	}

	// Server
	while(1) {
		// acquire a connection
		int connfd = accept(listenfd, (struct sockaddr*) NULL, NULL);

		// find out what the connection is made for
		int stepType = checkStepType(connfd);

		// if the connection was made for the check in step
		if (stepType == 1) {
			tryOpen(connfd);

		// if the connection was made for the fileSizeStep
		} else if (stepType == 2) {
			getFileSize(connfd);

		// if the connection was made for the download step
		} else if (stepType == 3) {
			sendChunk(connfd);

		// if the connection was made for something else
		} else {
			printf("ERROR: Extremely unexpected step type!\n");
			exit(1);
		}

		/*
		Close the connection, regardless of whether or not the step succeeded
		If something happened that wasn't supposed to, the client should handle
			it. We don't want our server to go down for a bad reason.
		*/
		close(connfd);
	}

	// shouldn't happen, but the compiler would complain otherwise
	return 0;
}
