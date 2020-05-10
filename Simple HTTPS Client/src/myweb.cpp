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
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <vector>
#include <string>

using namespace std;

	
// global variables
string hostName;
string targetPort;
string targetResource;


/*
example input:
./myweb https://www.example.com:443/index.html
*/


/*
Purpose: Check whether the client wants to use HTTP or HTTPS.
Parameters:
	- url: The url to parse
Returns:
	- The location of the protocol in the url, which is used later.
*/
int determineProto(string url) {
	if (url.find("https://") != -1) {
		return 8;
	} else if (url.find("http://") != -1) {
		return 7;
	} else {
		return 0;
	}
}


/*
Purpose: Extracts the port, host name/IP, and target resource from the url.
Parameters:
	- initialOffset: Determines where to start searching the string
	- url: The url to parse
Returns:
	- 0: Sucess
	- -1: Something went wrong trying to parse the URL. See the error for details.
*/
int parseArgs (int initialOffset, string url, bool https) {
	// the location of the marker for resource
	int firstSlash = url.find("/", initialOffset);
	
	// search for the port
	int portLocation = url.find(":", initialOffset) + 1;
	if (portLocation == 0) {
		// set a default port based on protocol
		if (https) {
			targetPort = "443";
		} else {
			targetPort = "80";
		}
	}

	// extract the host name or IP
	if (portLocation != 0) {
		hostName = url.substr(initialOffset, (portLocation-1) - initialOffset);
		targetPort = url.substr(portLocation, firstSlash - portLocation);
	} else {
		hostName = url.substr(initialOffset, firstSlash - initialOffset);
	}

	// extract the target resource
	if (firstSlash == url.size() || firstSlash == -1) {
		targetResource = "/";
	} else {
		targetResource = url.substr(firstSlash);
	}

	return 0;
}

// code based upon man page of getAddr info at https://linux.die.net/man/3/getaddrinfo
/*
Purpose: Lookup the IP address of a specified host.
Parameters:
	- None. hostName is a global variable.
Returns:
	- testSocket: The socket that we established a connection on.
*/
int dnsLookup() {
	// the socket we will use to test the returned addresses with
	int testSocket;

	// what we will use to query with
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));

	// where the response will be stored
    struct addrinfo* result;
	struct addrinfo* rp;

	// populate the hints
	hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

	// perform the query 
	int addrInfoStatus = getaddrinfo(hostName.c_str(), targetPort.c_str(), &hints, &result);
	if (addrInfoStatus != 0) {
        fprintf(stderr, "FATAL ERROR: (dnsLookup) (getaddrinfo): %s\n", gai_strerror(addrInfoStatus));
        exit(EXIT_FAILURE);
	}

	// find the valid response
	for (rp = result; rp != NULL; rp = rp->ai_next) {
        // try making a socket to each IP
		testSocket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (testSocket == -1) {
            continue;
		}

		// try making a connection to the socket if it is valid
		if (connect(testSocket, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;
		}

       close(testSocket);
    }
	
	// if we could not find an A type record
	if (rp == NULL) {
        freeaddrinfo(result);
		exit(EXIT_FAILURE);
    }

   freeaddrinfo(result);
   return testSocket;
}


/*
Purpose: Determine the content length of the returned request.
Parameters:
	- Buffer: The header of the message.
Returns:
	- result: The content length extracted from the message.
*/
int getContentLength (string buffer) {
	int locationStart = buffer.find("Content-Length") + 16;
	int locationEnd = buffer.find("\r\n\r\n");
	
	string result = buffer.substr(locationStart, (locationEnd - locationStart));
	return stoi(result);
}


/*
Purpose: Creates a generic GET request.
Parameters:
	- None (all are global variables)
Returns:
	- result: The HTTP GET request.
*/
string createGetRequest() {
	string result = "GET " + targetResource + " HTTP/1.1\r\nHost: " +
	hostName + "\r\n\r\n";
	return result;
}


/*
Purpose: Creates a generic HEAD request.
Parameters:
	- None (all are global variables)
Returns:
	- result: The HTTP GET request.
*/
string createHeadRequest() {
	string result = "HEAD " + targetResource + " HTTP/1.1\r\nHost: " +
	hostName + "\r\n\r\n";
	return result;
}


/*
Purpose: Extracts the first message beyond the end of the header.
Parameters:
	- msg: The message to parse
Returns:
	- result: The actual content after the header
*/
string getFirstMessage(string msg) {
	int start = msg.find("\r\n\r\n") + 4;
	return msg.substr(start);
}



/*
Purpose: Creates and sends an HTTP request to the server. Then, reads the response and
	puts it into the appropriate place.
Parameters:
	- httpSocket: The socket that the connection has been made on
	- headRequest: Whether to make a GET (false) or HEAD (true) request.
Return:
	- 0: success
	- -1: Failure
*/
int processHTTPRequest(int httpSocket, bool headRequest) {
	string request;
	if (headRequest) {
		request = createHeadRequest();
	} else {
		request = createGetRequest();
	}

	// send the request
	if (write(httpSocket, request.c_str(), request.size()) == -1) {
		printf("ERROR (processHTTPRequest) (write) (%d): %s\n", errno, strerror(errno));
		return -1;
	}

	// read in the respnose
	char* response = (char*) calloc(4096, sizeof(char));
	int responseSize = read(httpSocket, response, 4096);
	if (responseSize == -1) {
		printf("ERROR (processHTTPRequest) (read) (%d): %s\n", errno, strerror(errno));
		free(response);
		return -1;
	}

	// if GET request
	if (headRequest == false) {
		// open a file to write to
		int outFile = open("output.dat", O_RDWR | O_CREAT, S_IRWXU);
		if (outFile == -1) {
			printf("ERROR (processHTTPRequet (open) (%d): %s\n", errno, strerror(errno));
			free(response);
			return -1;
		}

		// extract the contentLength and the content of the first response
		int contentLength = getContentLength(string(response));
		string firstMessage = getFirstMessage(string(response));

		// write to the file
		int amountWritten = write(outFile, firstMessage.c_str(), firstMessage.size());
		if (amountWritten == -1) {
			printf("ERROR (processHTTPRequet (write) (%d): %s\n", errno, strerror(errno));
			free(response);
			return -1;
		}

		// update the contentLength
		contentLength -= amountWritten;

		// while there is stll content left to write
		while (contentLength != 0) {
			// read in more from the socket
			memset(response, '\0', 4096);
			int amountRead = read(httpSocket, response, 4096);
			if (amountRead == -1) {
				printf("ERROR (processHTTPRequet (read) (%d): %s\n", errno, strerror(errno));
				free(response);
				return -1;
			}

			// write to file
			amountWritten = write(outFile, response, amountRead);
			if (amountWritten == -1) {
				printf("ERROR (processHTTPRequet (write) (%d): %s\n", errno, strerror(errno));
				free(response);
				return -1;
			}

			// recalculate how much we have left to write to the file
			contentLength -= amountWritten;
		}
		return 0;
	}

	// if headRequest
	string responseStr = string(response);
	
	// see if we have the whole header
	int checkEnd = responseStr.find("\r\n\r\n");
	if (checkEnd != -1) {
		if (write(1, response, responseSize) == -1) {
			printf("ERROR (processHTTPRequest) (write) (%d): %s\n", errno, strerror(errno));
			free(response);
			return -1;
		}
	
	// if not, then keep write what we have so far
	} else {
		if (write(1, response, responseSize) == -1) {
			printf("ERROR (processHTTPRequest) (write) (%d): %s\n", errno, strerror(errno));
			free(response);
			return -1;
		}

		// keep reading and writing until we have reached the end of the header
		while(checkEnd == -1) {
			memset(response, '\0', 4096);
			
			// read
			responseSize = read(httpSocket, response, 4096);
			if (responseSize == -1) {
				printf("ERROR (processHTTPRequest) (read) (%d): %s\n", errno, strerror(errno));
				free(response);
				return -1;
			}

			// recalculate if we have reached the end
			checkEnd = responseStr.find("\r\n\r\n");

			// write
			if (write(1, response, responseSize) == -1) {
				printf("ERROR (processHTTPRequest) (write) (%d): %s\n", errno, strerror(errno));
				free(response);
				return -1;
			}
		}
	}

	free(response);
	return 0;
}


// code based upon the tutorial at https://www.ibm.com/support/knowledgecenter/en/SSB23S_1.1.0.15/gtps7/s5sple2.html
/*
Purpose: Creates and sends an HTTPS request to the server. Then, reads the response and
	puts it into the appropriate place.
Parameters:
	- httpsSocket: The socket that the connection has been made on
	- headRequest: Whether to make a GET (false) or HEAD (true) request.
Return:
	- 0: success
	- -1: Failure
*/
int processHTTPSRequest(int httpsSocket, bool headRequest) {
	// create the request
	string request;
	if (headRequest) {
		request = createHeadRequest();
	} else {
		request = createGetRequest();
	}

	// intialize the SSL connection
	const SSL_METHOD* method = TLSv1_2_client_method();
	SSL_CTX* myCTX = SSL_CTX_new(method);
	SSL* mySSL = SSL_new(myCTX);
	SSL_set_fd(mySSL, httpsSocket);

	int	checkErr = SSL_connect(mySSL);
	if (checkErr < 1) {
		checkErr = SSL_get_error(mySSL, checkErr);
		printf("ERROR (processHTTPSRequest) (SSL_connect): %d\n", checkErr);
		SSL_free(mySSL);
   		SSL_CTX_free(myCTX);
		return -1;
	}


	// send the request
	checkErr = SSL_write(mySSL, request.c_str(), request.size());
	if (checkErr < 1) {
		checkErr = SSL_get_error(mySSL, checkErr);
		printf("ERROR (processHTTPSRequest) (SSL_connect): %d\n", checkErr);
		SSL_free(mySSL);
   		SSL_CTX_free(myCTX);

		// the server sent a SSL_shutdown
		if (checkErr == 6) {
			SSL_shutdown(mySSL);
		}

		return -1;
	}

	// read in the server's response
	char* response = (char*) calloc(4096, sizeof(char));
	int responseSize = SSL_read(mySSL, response, 4096);
	if (responseSize < 1) {
		checkErr = SSL_get_error(mySSL, checkErr);
		printf("ERROR (processHTTPRequest) (SSL_read): %d\n", checkErr);
		free(response);
		if (checkErr == 6) {
			SSL_shutdown(mySSL);
		}
		SSL_free(mySSL);
		SSL_CTX_free(myCTX);
		return -1;
	}

	// if GET request
	if (headRequest == false) {
		// open a file to write to
		int outFile = open("output.dat", O_RDWR | O_CREAT, S_IRWXU);
		if (outFile == -1) {
			printf("ERROR (processHTTPSRequet (open) (%d): %s\n", errno, strerror(errno));
			SSL_free(mySSL);
			SSL_CTX_free(myCTX);
			free(response);
			return -1;
		}

		// extract the contentLength and the content of the first response
		int contentLength = getContentLength(string(response));
		string firstMessage = getFirstMessage(string(response));

		// write to the file
		int amountWritten = write(outFile, firstMessage.c_str(), firstMessage.size());
		if (amountWritten == -1) {
			printf("ERROR (processHTTPRequet (write) (%d): %s\n", errno, strerror(errno));
			SSL_free(mySSL);
			SSL_CTX_free(myCTX);
			close(outFile);
			free(response);
			return -1;
		}

		// update the contentLength
		contentLength -= amountWritten;

		// while there is stll content left to write
		while (contentLength != 0) {
			// read in more from the socket
			memset(response, '\0', 4096);
			int amountRead = SSL_read(mySSL, response, 4096);
			if (amountRead < 1) {
				checkErr = SSL_get_error(mySSL, amountRead);
				printf("ERROR (processHTTPRequest) (SSL_read): %d\n", checkErr);
				free(response);
				if (checkErr == 6) {
					SSL_shutdown(mySSL);
				}
				SSL_free(mySSL);
				SSL_CTX_free(myCTX);
				close(outFile);
				return -1;
			}

			// write to file
			amountWritten = write(outFile, response, amountRead);
			if (amountWritten == -1) {
				printf("ERROR (processHTTPRequet (write) (%d): %s\n", errno, strerror(errno));
				SSL_free(mySSL);
				SSL_CTX_free(myCTX);
				free(response);
				close(outFile);
				return -1;
			}

			// recalculate how much we have left to write to the file
			contentLength -= amountWritten;
		}

		// intitiate a graceful shutdown
		checkErr = SSL_shutdown(mySSL);
		if (checkErr < 0) {
			checkErr = SSL_get_error(mySSL, checkErr);
			printf("ERROR (processHTTPRequest) (SSL_shutdown): %d\n", checkErr);
			close(outFile);
			free(response);
			if (checkErr == 6) {
				SSL_shutdown(mySSL);
			}
			SSL_free(mySSL);
			SSL_CTX_free(myCTX);
			return -1;
		}
	
		close(outFile);
		free(response);
		SSL_free(mySSL);
		SSL_CTX_free(myCTX);
		return 0;
	}

	// if headRequest
	string responseStr = string(response);
	
	// see if we have the whole header
	int checkEnd = responseStr.find("\r\n\r\n");
	if (checkEnd != -1) {
		if (write(1, response, responseSize) == -1) {
			printf("ERROR (processHTTPRequest) (write) (%d): %s\n", errno, strerror(errno));
			free(response);
			SSL_free(mySSL);
			SSL_CTX_free(myCTX);
			return -1;
		}
	
	// if not, then keep write what we have so far
	} else {
		if (write(1, response, responseSize) == -1) {
			printf("ERROR (processHTTPRequest) (write) (%d): %s\n", errno, strerror(errno));
			SSL_free(mySSL);
			SSL_CTX_free(myCTX);
			free(response);
			return -1;
		}

		// keep reading and writing until we have reached the end of the header
		while(checkEnd == -1) {
			memset(response, '\0', 4096);
			
			// read
			responseSize = SSL_read(mySSL, response, 4096);
			if (responseSize < 1) {
				checkErr = SSL_get_error(mySSL, responseSize);
				printf("ERROR (processHTTPRequest) (SSL_read): %d\n", checkErr);
				free(response);
				if (checkErr == 6) {
					SSL_shutdown(mySSL);
				}
				SSL_free(mySSL);
				SSL_CTX_free(myCTX);
				return -1;
			}
			

			// recalculate if we have reached the end
			checkEnd = responseStr.find("\r\n\r\n");

			// write
			if (write(1, response, responseSize) == -1) {
				printf("ERROR (processHTTPRequest) (write) (%d): %s\n", errno, strerror(errno));
				free(response);
				SSL_free(mySSL);
				SSL_CTX_free(myCTX);
				return -1;
			}
		}
	}
	
	// intitiate a graceful shutdown
	checkErr = SSL_shutdown(mySSL);
	if (checkErr < 0) {
		checkErr = SSL_get_error(mySSL, checkErr);
		printf("ERROR (processHTTPRequest) (SSL_shutdown): %d\n", checkErr);
		free(response);
		if (checkErr == 6) {
			SSL_shutdown(mySSL);
		}
		SSL_free(mySSL);
		SSL_CTX_free(myCTX);
		return -1;
	}

	free(response);
	SSL_free(mySSL);
	SSL_CTX_free(myCTX);	
	return 0;
}


int main(int argc, const char* argv[]) {
	// check for a valid number of arguments
	if (argc < 2 || argc > 3) {
		printf("ERROR (input): Invalid arguments. See README for details.\n");
		exit(EXIT_FAILURE);
	}

	// check whether or not we are performing a head request
	bool headRequest = false;
	if (argc == 3) {
		if (strcmp(argv[2], "-h") == 0 ) {
			headRequest = true;
		} else {
			printf("ERROR (input): Invalid second argument. Did you mean -h?\n");
			exit(EXIT_FAILURE);
		}
	}

	// determine if we are using HTTPS or HTTP
	bool https = false;
	int protocol = determineProto(string(argv[1]));
	if (protocol == 8) {
		https = true;
	}

	// extract the hostname, port, and URL
	if (parseArgs(protocol, string(argv[1]), https) == -1) {
		printf("ERROR (input): Unable to find all components of the url.\n");
		exit(EXIT_FAILURE);
	}

	// for making the socket if it is HTTP based
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family=AF_INET;
	addr.sin_port=htons(stoi(targetPort));

	// check if the hostname is an IP or a hostname
	int inet_ptonReturn = inet_pton(AF_INET, hostName.c_str(), &(addr.sin_addr));

	// socket we will use to connect to the site with
	int connSocket;

	// if the hostName is just a hostname
	if (inet_ptonReturn == 0) {
		connSocket = dnsLookup();

	// if the hostName is an IP, establish a socket
	} else if (inet_ptonReturn == 1) {
		connSocket = socket(AF_INET, SOCK_STREAM, 0);
		if (connSocket == -1) {
			printf("FATAL ERROR (socket) (%d): %s\n", errno, strerror(errno));
			exit(EXIT_FAILURE);
		}
		if (connect(connSocket, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
			close(connSocket);
			printf("FATAL ERROR (connect) (%d): %s\n", errno, strerror(errno));
			exit(EXIT_FAILURE);
		}

	// if the hostName extracted is not an IP nor a valid hostName
	} else {
		printf("FATAL ERROR (inet_pton) (%d): %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	if (https) {
		// process the request if it is HTTPS
		OpenSSL_add_all_algorithms();
		SSL_load_error_strings();
		SSL_library_init();
		if (processHTTPSRequest(connSocket, headRequest) == -1) { 
			printf("FATAL ERROR: Unable to perform the request.\n");
			CRYPTO_cleanup_all_ex_data();
			ERR_free_strings();
			ERR_remove_state(0);
			EVP_cleanup();
			close(connSocket);
			SSL_COMP_free_compression_methods();
			exit(EXIT_FAILURE);
		}
		CRYPTO_cleanup_all_ex_data();
		ERR_free_strings();
		ERR_remove_state(0);
		EVP_cleanup();
		SSL_COMP_free_compression_methods();
		
	} else {
		// process the request if it is HTTP
		if (processHTTPRequest(connSocket, headRequest) == -1) {
			printf("FATAL ERROR: Unable to perform the request.\n");
			close(connSocket);
			exit(EXIT_FAILURE);
		}
	}
	close(connSocket);
	exit(EXIT_SUCCESS);
}