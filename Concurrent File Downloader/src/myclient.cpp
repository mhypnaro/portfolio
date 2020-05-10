#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace std;

// global variables required by mulitple threads at the same time
string fileName;
int outputFile;

// Struct to pass in arguments to a thread.
struct threadArgs {
	const char* IP;
	int port;
	int startLoc;
	int amountToRead;
};

/*
	Struct to return information from a thread.
	Useful for error handling.
*/
struct threadReturns {
	int status;
	int startLoc;
	int amountToRead;
	int amountRead;
};


/*
Corresponds to the client side Check-In step.
Purpose: Determines whether or not the client can connect with the server.
	Also checks whether or not the server has the file specified.
Parameters: (passed in the form of a threadArgs struct pointer)
	IP: IP of the server to contact.
	port: Port of the server to contact.
Returns:
	0: Success
	-1: Something went wrong. Doesn't matter what, it makes the (IP, port) pair irrelevant.
*/
void* checkIn(void* input) {
	// Thread Variables
	int id = pthread_self();
	int* returnValue = (int*) calloc(1, sizeof(int));
	struct threadArgs* args = (struct threadArgs*) input;
	const char* checkInIP = args->IP;
	int checkInPort = args->port;

	// Socket creation
	int checkInSoc;
	checkInSoc = socket(AF_INET, SOCK_STREAM, 0);
	if (checkInSoc == -1) {
		printf("(thread %d) ERROR (socket) (%d): %s\n", id, errno, strerror(errno));
		*returnValue = -1;
		return returnValue;
	}

	// Address struct creation
	struct sockaddr_in addr;
	bzero(&addr, sizeof addr);
	addr.sin_family=AF_INET;
	addr.sin_port=htons(checkInPort);

	// converts an ip address to the format that the struct expects
	if (inet_pton(AF_INET,checkInIP,&(addr.sin_addr)) == -1) {
		printf("(thread %d) ERROR (inet_pton) (%d): %s\n", id, errno, strerror(errno));
		*returnValue = -1;
		close(checkInSoc);
		return returnValue;
	}

	// attempt to connect to the server
	if (connect(checkInSoc,(struct sockaddr *)&addr,sizeof(addr)) == -1) {
		printf("(thread %d) ERROR (connect) (%d): %s\n", id, errno, strerror(errno));
		*returnValue = -1;
		close(checkInSoc);
		return returnValue;
	}

	// The Check-In step message format
	string msg = "Do you have: " + fileName + "?";

	// Read in the response
	char* response = (char*) calloc (2, sizeof(char));
	if (write(checkInSoc, msg.c_str(), msg.size()) == -1) {
		printf("ERROR: (thread %d) (checkIn) (read) (%d): %s\n", id, errno, strerror(errno));
		*returnValue = -1;
		close(checkInSoc);
		return returnValue;
	}
	if (read(checkInSoc, response, 2) == -1) {
		printf("ERROR: (thread %d) (checkIn) (read) (%d): %s\n", id, errno, strerror(errno));
		*returnValue = -1;
		close(checkInSoc);
		return returnValue;
	}

	// The server has the file, all good!
	if (strcmp(response, "Y") == 0) {
		*returnValue = 0;
		close(checkInSoc);
		return returnValue;
	}
	
	// the server sent us an "N" or something else not "Yes"
	else {
		*returnValue = -1;
		close(checkInSoc);
		return returnValue;
	}
}


/*
Corresponds to the client side File size step.
Purpose: Sends a message to ONE server asking for the size of the file
Parameters:
	fileSizeIP: The IP of the server to query
	fileSizePort: The port of the server to query
Returns:
	The file size of the request file.
	-1: If an error occurs.
*/
int getFileSize(const char* fileSizeIP, int fileSizePort) {
	// Socket creation
	int fileSizeSoc;
	fileSizeSoc = socket(AF_INET, SOCK_STREAM, 0);
	if (fileSizeSoc == -1) {
		printf("ERROR (getFileSize) (socket) (%d): %s\n", errno, strerror(errno));
		return -1;
	}

	// Address struct creation
	struct sockaddr_in addr;
	bzero(&addr, sizeof addr);
	addr.sin_family=AF_INET;
	addr.sin_port=htons(fileSizePort);

	// converts an ip address to the format that the struct expects
	if (inet_pton(AF_INET,fileSizeIP,&(addr.sin_addr)) == -1) {
		printf("ERROR (getFileSize) (inet_pton) (%d): %s\n", errno, strerror(errno));
		close(fileSizeSoc);
		return -1;
	}

	// connect to the server
	if (connect(fileSizeSoc,(struct sockaddr *)&addr,sizeof(addr)) == -1) {
		printf("ERROR (getFileSize) (connect) (%d): %s\n", errno, strerror(errno));
		close(fileSizeSoc);
		return -1;
	}
	
	// The File size step message format
	string msg = "Filesize of: " + fileName + "?";

	// read in the server's response
	char* response = (char*) calloc (1024, sizeof(char));
	if (write(fileSizeSoc, msg.c_str(), msg.size()) == -1) {
		printf("ERROR: (getFileSize) (write) (%d): %s\n", errno, strerror(errno));
		close(fileSizeSoc);
		return -1;
	}
	if (read(fileSizeSoc, response, 1024) == -1) {
		printf("ERROR: (getFileSize) (read) (%d): %s\n", errno, strerror(errno));
		close(fileSizeSoc);
		return -1;
	}
	close(fileSizeSoc);
	return atoi(response);
}

/*
Corresponds to the client side Download step.
Purpose: Tells the server to send the client a portion of a file
Parameters: (passed in the form of a threadArgs struct pointer)
	IP: What IP to connect to
	port: What port to connect to
	startLoc: The offset to begin reading at
	amountToRead: How much this thread should read
Returns: (returned in the form of a threadReturns struct pointer)
	status:
		-1: error
		0: success
	amountToRead: How much is left to read in this chunk
	amountRead: How much has been read in this call to the function
	startLoc: the offset this thread made it to when the function terminated
	
*/
void* download (void* input) {
	// Thread Variables
	int id = pthread_self();
	struct threadArgs* args = (struct threadArgs*) input;
	const char* downloadIP = args->IP;
	int downloadPort = args->port;
	int startLoc = args->startLoc;
	int amountToRead = args->amountToRead;

	// intitailize the return values
	struct threadReturns* returnValue = (struct threadReturns*) calloc (1, sizeof(struct threadReturns));
	returnValue->status = 0;
	returnValue->amountToRead = amountToRead;
	returnValue->startLoc = startLoc;
	returnValue->amountRead = 0;
	
	// Socket creation
	int downloadSoc;
	downloadSoc = socket(AF_INET, SOCK_STREAM, 0);
	if (downloadSoc == -1) {
		printf("ERROR (thread %d) (download) (socket) (%d): %s\n", id, errno, strerror(errno));
		returnValue -> status = -1;
		return returnValue;
	}

	// Address struct creation
	struct sockaddr_in addr;
	bzero(&addr, sizeof addr);
	addr.sin_family=AF_INET;
	addr.sin_port=htons(downloadPort);

	// converts an ip address to the format that the struct expects
	if (inet_pton(AF_INET,downloadIP,&(addr.sin_addr)) == -1) {
		printf("ERROR (thread %d) (download) (inet_pton) (%d): %s\n", id, errno, strerror(errno));
		close(downloadSoc);
		returnValue->status = -1;
		return returnValue;
	}

	// connect to the server
	if (connect(downloadSoc,(struct sockaddr *)&addr,sizeof(addr)) == -1) {
		printf("ERROR (thread %d) (download) (connect) (%d): %s\n", id, errno, strerror(errno));
		close(downloadSoc);
		returnValue->status = -1;
		return returnValue;
	}
	
	// Format of the Download step message
	string msg = "Send: " + to_string(amountToRead) + " bytes of: " + fileName + " starting from: " + to_string(startLoc) + ".";
	char* response = (char*) calloc (1024, sizeof(char));

	// send the mssage
	if (write (downloadSoc, msg.c_str(), msg.size()) == -1) {
		printf("ERROR (thread %d) (download) (write) (%d): %s\n", id, errno, strerror(errno));
		close(downloadSoc);
		returnValue->status = -1;
		return returnValue;
	}
	
	// keep track of how much left we have to read and write
	int amountReceived;
	int amountWritten;
	int offset = startLoc;
	bool contentLeft = true;

	/*While there is still something left to read...
	If there is an error, return with status set to -1 and let the downloadStep function handle it
	*/
	while (contentLeft == true) {

		// if the amount left to read can not fit in our allocated buffer
		if (amountToRead > 1024) {
			amountReceived = read (downloadSoc, response, 1024);
			if (amountReceived == -1) {
				printf("ERROR (thread %d) (download) (read) (%d): %s\n", id, errno, strerror(errno));
				close(downloadSoc);
				returnValue->status = -1;
				returnValue->startLoc = offset;
				returnValue->amountToRead = amountToRead;
				return returnValue;
			} else if (amountReceived == 0) {
				printf("ERROR (thread %d) (download) (read): Received 0 bytes.\n", id);
				close(downloadSoc);
				returnValue->status = -1;
				returnValue->startLoc = offset;
				returnValue->amountToRead = amountToRead;
				return returnValue;
			}

			// write as much data as we have recieved to our output file
			amountWritten = pwrite (outputFile, response, amountReceived, offset);
			returnValue->amountRead += amountReceived;
			if (amountWritten == -1) {
				printf("ERROR (thread %d) (download) (write) (%d): %s\n", id, errno, strerror(errno));
				close(downloadSoc);
				returnValue->status = -1;
				returnValue->startLoc = offset;
				returnValue->amountToRead = amountToRead;
				return returnValue;
			}

			// change how much we have left to read and from what offset
			offset += amountReceived;
			amountToRead -= amountReceived;
			memset(response, 0, 1024);

		// if the amount left to read can fit in our allocated buffer
		} else if (amountToRead <= 1024) {
			amountReceived = read (downloadSoc, response, amountToRead);
			if (amountReceived == -1) {
				printf("ERROR (thread %d) (download) (read) (%d): %s\n", id, errno, strerror(errno));
				close(downloadSoc);
				returnValue->status = -1;
				returnValue->startLoc = offset;
				returnValue->amountToRead = amountToRead;
				return returnValue;
			} else if (amountReceived == 0) {
				printf("ERROR (thread %d) (download) (read): Received 0 bytes.\n", id);
				close(downloadSoc);
				returnValue->status = -1;
				returnValue->startLoc = offset;
				returnValue->amountToRead = amountToRead;
				return returnValue;
			}

			// write as much data as we have recieved to our output file
			amountWritten = pwrite (outputFile, response, amountReceived, offset);
			returnValue->amountRead += amountReceived;
			if (amountWritten == -1) {
				printf("ERROR (thread %d) (download) (write) (%d): %s\n", id, errno, strerror(errno));
				close(downloadSoc);
				returnValue->status = -1;
				returnValue->startLoc = offset;
				returnValue->amountToRead = amountToRead;
				return returnValue;
			}

			// change how much we have left to read and from what offset
			offset += amountReceived;
			amountToRead -= amountReceived;
			memset(response, 0, 1024);

			// if we have read all we were supposed to with this thread...
			if (amountToRead == 0) {
				returnValue->startLoc = offset;
				returnValue->amountToRead = 0;
				contentLeft = false;
			}
		}
	}
	close (downloadSoc);
	return returnValue;
}


/*
Purpose: Calculates the number of bytes each chunk should be.
Parameters:
	numDownloadConnections: How many (IP, port) pairs are still valid
	fileSize: The total size of the file returned in the File size step
Return:
	downloadSize: An array of ints that spcifies how much each thread should read
*/
int* caluclateDownloadSize (int numDownloadConnections, int fileSize) {
	// initialize the return value
	int* downloadSize = (int*) calloc(numDownloadConnections, sizeof(int));
	
	// used for the loop
	int iteratorFileSize = fileSize;

	// how much each thread should read if it isn't the last thread
	int amountPerThread = fileSize / numDownloadConnections;

	for (int i = 0; i < numDownloadConnections; i++) {
		// if this is the last thread, tell it to read whatever is left over
		if (i == numDownloadConnections - 1) {
			downloadSize[i] = iteratorFileSize;
			iteratorFileSize -= iteratorFileSize;
		} else {
			downloadSize[i] =  amountPerThread;
			iteratorFileSize -= amountPerThread;
		}
	}
	return downloadSize;
}

/*
Corresponds to the client side Download step
Purpose: Determines the parameters for each thread to call download with.
	Also handles a variety of possible errors.
Parameters:
	downloadServerIPs: list of server IPs still valid as download sources
	downloadServerPorts: List of server ports still valids as download sources
	numDownloadConnections: How many (IP, port) pairs are still valid
	fileSize: The total size of the file returned in the File size step.
	downloadThreads: The threads we allocated in main.
Returns:
	0: Our client has successfully retrieved the file
	-1: Our client could not retireve the file despite all of our error handling
*/
int downloadStep (vector <string> downloadServerIPs, vector <int> downloadServerPorts, int numDownloadConnections, int fileSize, pthread_t* downloadThreads) {
	// calculate the amount each thread is intended to read
	int* downloadSize = caluclateDownloadSize(numDownloadConnections, fileSize);

	// Open the file we will right to now (no need to open it earlier)
	outputFile = open(fileName.c_str(), O_RDWR | O_CREAT, S_IRWXU);
	if (outputFile == -1) {
		printf("FATAL ERROR: The file you requested could not be created in the client directory.\n");
		printf("FATAL ERROR: The error is (%d): %s\n", errno, strerror(errno));
		exit(1);
	}

	// intitiate the arguments our threads will use
	struct threadArgs* downloadArgs = (struct threadArgs*) calloc(numDownloadConnections, sizeof(struct threadArgs));
	
	// change the offset based on how much previous thread is going to read
	int threadOffset = 0;

	// spawn the threads with the proper arguments to retrieve the chunks
	for (int i = 0; i < numDownloadConnections; i++) {
		downloadArgs[i].IP = downloadServerIPs.at(i).c_str();
		downloadArgs[i].port = downloadServerPorts.at(i);
		downloadArgs[i].startLoc = threadOffset;
		downloadArgs[i].amountToRead = downloadSize[i];
		threadOffset += downloadSize[i];
		pthread_create(&downloadThreads[i], NULL, download, (void *)(&downloadArgs[i]));
	}

	// intialize and read in the result of each threads attempt
	struct threadReturns** downloadStatus = (struct threadReturns**) calloc(numDownloadConnections, sizeof(struct threadReturns));
	for (int i = 0; i < numDownloadConnections; i++) {
		pthread_join(downloadThreads[i], (void**)&downloadStatus[i]);
	}

	/*
	We manage errors based on downloadStatus and how much is left to be read.
	Download status:
		-1: Error. The thread did not fully read what it was supposed to.
		0: The thread read what it was supposed to, but the amount it read has not yet
			been added to "totalRead"
		1: The thread read what it was supposed to and the amount it read has already
			been added to "totalRead"
		2: The thread is busy fixing the mistake of another thread right now.
		3: This thread has a bad connection which must not be used again.
	*/

	// how much we have read so far-- used to know when the client is done with the request
	int totalRead = 0;

	// used for determining whether or not or client should commit suicide (fail) or not
	bool errorResolved;

	// used for determning what threads have been dispatched to handle an error
	vector<int> indexes;

	// while we haven't fully read the file
	while (totalRead < fileSize) {
		/*
		Intialize to false at the beginning of each loop,
		but the condition isn't checked unless there is an error.
		*/
		errorResolved = false;

		// for each thread
		for (int i = 0; i < numDownloadConnections; i++) {
			
			// if the thread hasn't updated totalRead yet, update it and set status to 1
			if (downloadStatus[i]->status == 0) {
				totalRead += downloadStatus[i]->amountRead;
				downloadStatus[i]->status = 1;

			// if the thread had an error
			} else if (downloadStatus[i]->status == -1) {
				
				// update total read with what we have read so far
				if (downloadStatus[i]->amountRead > 0) {
					totalRead += downloadStatus[i]->amountRead;
				}

				// search for a thread to finish the download that failed
				for (int j = 0; j < numDownloadConnections; j++) {

					// look for a thread which did not get flagged for an error
					if (i != j && (downloadStatus[j]->status == 1 || downloadStatus[j]->status == 0)) {
						
						// mark the thread that caused the error as bad
						downloadStatus[i]->status = 3;
						
						// thread j will be spawned and joined later
						indexes.push_back(j);

						// if thread j has not contributed to totalRead, do it now
						if (downloadStatus[j]->status == 0) {
							totalRead += downloadStatus[j]->amountRead;
						}

						// Set the arguments of thread j to start where the failed thread left off
						downloadArgs[j].startLoc = downloadStatus[i]->startLoc;
						downloadArgs[j].amountToRead = downloadStatus[i]->amountToRead;
						
						// Set the status of this thread as 2, so it does not try to handle the error of another bad thread
						downloadStatus[j]->status = 2;

						// We found a thread to handle the error, break
						errorResolved = true;
						break;
					}
				}

				// we did not find a thread to handle the error, the client failed
				if (errorResolved == false) {
					return -1;
				}
			}
		}

		// dispatch each error-handling thread to finish the requests assigned to them
		for (int i = 0; i < indexes.size(); i++) {
			pthread_create(&downloadThreads[indexes.at(i)], NULL, download, (void *)(&downloadArgs[indexes.at(i)]));
		}

		// wait for each error handlnig thread to finish
		for (int i = 0; i < indexes.size(); i++) {	
			pthread_join(downloadThreads[indexes.at(i)], (void**)&downloadStatus[indexes.at(i)]);
		}

		// all error handling threads have finished one way or another
		indexes.clear();
	}

	// we read everything we were supposed to in the right order, so the client succeeded
	return 0;
}


/*
Purpose: Parses input and performs each step
Returns:
	0: success
	1: failure
*/
int main(int argc, char* argv[]) {
	// validate the number of arugments
	if (argc < 4 || argc > 4) {
		printf("FATAL ERROR (input): Invalid number of arguments. See README for details.\n");
		exit(1);
	}

	// get and validate the number of connections to open
	int numConnections = atoi(argv[2]);
	if (numConnections == 0 || numConnections < 0) {
		printf("FATAL ERROR (input): Invalid number of connections \"%s\"\n", argv[2]);
		exit(1);
	}

	// get the name of the file to download
 	fileName = string(argv[3]);
	
	// attempt to open the server info text file
	FILE* serverInfo = fopen(argv[1], "r");
	if (serverInfo == NULL) {
		printf("FATAL ERROR (input): Unable to open file \"%s\" to read the server info.\n", argv[1]);
		printf("See README for details on expected arguments.\n");
		exit(1);
	}

	// a list of IPs and port numbers to attempt to conenct to
	vector<string> serverIPs;
 	vector<int> serverPorts;
	
	// temporary variables updated during fscanf
	char* inIP = (char*) calloc(20, sizeof(char));
	int inPort = 0;
	
	// parse the server info text file and populate the lists of server IPs and port numbers
	while(fscanf(serverInfo, "%s %d\n", inIP, &inPort) != EOF) {
		serverIPs.push_back(string(inIP));
		serverPorts.push_back(inPort);
	}

	// unallocate memory for a temp variable
	free(inIP);

	// check if the number of connections provided is too high
	if(numConnections > serverIPs.size()) {
		numConnections = serverIPs.size();
	}

	// perform the checkin step
	pthread_t threads[numConnections];
	struct threadArgs* checkInArgs = (struct threadArgs*) calloc(numConnections, sizeof(struct threadArgs));
	for (int i = 0; i < numConnections; i++) {
		checkInArgs[i].IP = serverIPs.at(i).c_str();
		checkInArgs[i].port = serverPorts.at(i);
		pthread_create(&threads[i], NULL, checkIn, (void *)(&checkInArgs[i]));
	}

	// read the return value of each thread into a status struct
	int* checkInStatus = (int*) calloc(numConnections, sizeof(int));
	for (int i = 0; i < numConnections; i++) {
		int* ret;
		pthread_join(threads[i], (void**)&ret);
		checkInStatus[i] = *ret;
	}

	// free the memory allocated for the checkin step
	free (checkInArgs);

	/*
	Update the list of available servers by modifying the list of ips, ports, and number of connections.
	We remove a server as a valid connection if it did not have the requested file or our client could
	not connect to it.
	*/
	int initialNumConnections = numConnections;
	for (int i = 0; i < initialNumConnections ; i++) {
		if (checkInStatus[i] == 0) {
			continue;
		} else {
			serverIPs[i] = "ERROR";
			serverPorts[i] = -1;
			numConnections--;
		}
	}
	free(checkInStatus);

	// If all threads failed the check in step, the client failed
	if (numConnections == 0) {
		printf("FATAL ERROR (input): That file was not found.\n");
		printf("FATAL ERROR (input): It's also possible you specified no valid servers.\n");
		exit(1);
	}

	/*
	Part of the above step to only download from valid sources.
	Method adapted from: https://www.techiedelight.com/erase-elements-vector-cpp/
	*/
	serverIPs.erase(remove(serverIPs.begin(), serverIPs.end(), "ERROR"), serverIPs.end());
	serverPorts.erase(remove(serverPorts.begin(), serverPorts.end(), -1), serverPorts.end());

	// Complete the fileSize step
	int fileSize = getFileSize(serverIPs[0].c_str(), serverPorts[0]);

	// Complete the download step
	int success = downloadStep(serverIPs, serverPorts, numConnections, fileSize, threads);

	// If we failed to download the file
	if (success == -1) {
		printf("FATAL ERROR: No server is available to process the request. Unable to complete request.\n");
		exit(1);
	}
	
	// File has been fully downloaded!
	exit(0);
}
