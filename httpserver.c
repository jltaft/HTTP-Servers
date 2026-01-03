#include <stdio.h>
#include <regex.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "listener_socket.h"
#include "iowrapper.h"
#include "protocol.h"
#include <errno.h>
#include <sys/stat.h>

struct Request {
    //request-line
    char *method;
    char *uri;
    char *version;

    //header-field-list
    char *key;
    int value;

    char *message;
    int remaining_bytes;
};

struct Request parse_request(char *buffer, ssize_t bytes_read);
void process_request(struct Request *request, int fd);
int get_request(struct Request *request);
int put_request(struct Request *request, int fd);
void freeRequest(struct Request *request);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        write(STDERR_FILENO, "Invalid Usage\n", 14);
        exit(1);
    }
    //converts argument to an int
    int port_number = atoi(argv[1]);
    //printf("port arg: %s\n", argv[1]);
    //printf("port num: %d\n")
    if (port_number < 1 || port_number > 65535) {
        write(STDERR_FILENO, "Invalid Port\n", 13);
        exit(1);
    }

    //parse port number
    //open/listen to socket with port number (bind socket)
    //START LOOP
    //accept connection from socket
    //process connection to protocol spec
    //	-might have errors with regex (not cover everything) //make sure to use REGFREE
    //		- regexec() will return array of rmatches, NULL if not found
    //	- int ls_accept : treat returned int as file descriptor (connection.fd)
    //	- process connection: read, write, close (is opened when accepted)
    //		- read (conn.fd, buffer, size)
    //		- parse header (error handling is a separate path here)
    //			- method, URI, version, header field list
    //			- process(method) depending on get or set (one function to handle both)
    //close connection
    //END LOOP
    Listener_Socket_t *sock = ls_new(port_number);

    if (sock == NULL) {
        //will have to probably change error message
        write(STDERR_FILENO, "Invalid Port\n", 13);
        //ls_delete(&sock);
        exit(1);
    }

    char buffer[4096] = { 0 };

    while (true) {
	printf("About to accept connection!\n");
        int conn_fd = ls_accept(sock);

        if (conn_fd == -1) {
            write(STDERR_FILENO, "Connection acception failed\n", 28);
            close(conn_fd);
       //     ls_delete(&sock);
            exit(1);
        }

        struct Request request;
        int total_read = 0;
        int bytes_read = 0;
        while (total_read < 4096) {
            bytes_read = read(conn_fd, buffer + total_read, 4096 - total_read);
	    printf("Bytes read in main: %d\n, Total read in main: %d\n", bytes_read, total_read);
            if (bytes_read == -1) {
                dprintf(
                    conn_fd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                ls_delete(&sock);
                close(conn_fd);
                exit(1);
            }

            if (bytes_read == 0) {
                break;
            }
            total_read += bytes_read;
            buffer[total_read] = '\0';

            if (strstr(buffer, "\r\n\r\n") != NULL) {
                break;
            }
        }
        //printf("Buffer after reading into buffer: %s\n", buffer);
        request = parse_request(buffer, total_read);
        process_request(&request, conn_fd);
    	freeRequest(&request);
    	printf("Closing conn_fd in main!\n");
	close(conn_fd);
	memset(buffer, '\0', sizeof(buffer));       
    }
    //
    //printf("Deleting socket!\n");
    ls_delete(&sock);
    return 0;
}

// //function that will use the listener socket to get arguments
struct Request parse_request(char *buffer, ssize_t bytes_read) {
    //printf("I'm in parse_request!\n");
    struct Request request;
    if (strstr(buffer, "HTTP/1.1\\r\\n") != 0) {
        //dprintf(fd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
        request.version = NULL;
        return request;
    }
    regex_t regex;
    int reti;

    regmatch_t rmatches[4]; // 3 variables plus 1 for 1-based indexing

    //format for request-line
    char *request_format = "^(" TYPE_REGEX ") (" URI_REGEX ") (" HTTP_REGEX ")\r\n";
    //TODO: Error handling if incorrect format
    //create regular expression
    reti = regcomp(&regex, request_format, REG_EXTENDED);

    //if the creation failed
    if (reti != 0) {
        request.method = NULL;
        regfree(&regex);
        return request;
    }

    //Parsing request-line
    if (regexec(&regex, buffer, 4, rmatches, 0) == 0) {
        //printf("I'm regexing!\n");
        // Block of code parses method, uri, and version and places it into Request struct
        request.method = calloc(rmatches[1].rm_eo - rmatches[1].rm_so + 1, sizeof(char));
        strncpy(request.method, buffer + rmatches[1].rm_so, rmatches[1].rm_eo - rmatches[1].rm_so);
        request.method[rmatches[1].rm_eo - rmatches[1].rm_so] = '\0';
        //printf("%s\n", request.method);

        request.uri = calloc(rmatches[2].rm_eo - rmatches[2].rm_so + 1, sizeof(char));
        strncpy(request.uri, buffer + rmatches[2].rm_so, rmatches[2].rm_eo - rmatches[2].rm_so);
        request.uri[rmatches[2].rm_eo - rmatches[2].rm_so] = '\0';
        memmove(request.uri, request.uri + 1, strlen(request.uri));
	//request.uri += 1; //removes / in front
            //printf("%s\n", request.uri);

        request.version = calloc(rmatches[3].rm_eo - rmatches[3].rm_so + 1, sizeof(char));
        strncpy(request.version, buffer + rmatches[3].rm_so, rmatches[3].rm_eo - rmatches[3].rm_so);
        request.version[rmatches[3].rm_eo - rmatches[3].rm_so] = '\0';

    } else {
        request.method = NULL;
        regfree(&regex);
        return request;
    }

    regfree(&regex);
    //printf("I'm starting to parse header!\n");
    regmatch_t hmatches[3];
    int i = 0;
    //format for header-field
    char *header_format = "([a-zA-Z0-9.-]{1,128}): ([ -~]{1,128})";

    //array to store headers
    //char headers[MAX_HEADER_LEN];

    //Skips request line
    char *header_start = strstr(buffer, "\r\n");
    if (header_start) {
        header_start += 2;
    } else {
		request.method = NULL;
        return request;
    }
    //printf("%s\n", header_start);

    reti = regcomp(&regex, header_format, REG_NEWLINE | REG_EXTENDED);
    if (reti != 0) {
        request.method = NULL;
        regfree(&regex);
        return request;
    }

    while (regexec(&regex, header_start, 3, hmatches, 0) == 0 && i < MAX_HEADER_LEN) {
        //parsing key
        request.key = calloc(hmatches[1].rm_eo - hmatches[1].rm_so + 1, sizeof(char));
        strncpy(
            request.key, header_start + hmatches[1].rm_so, hmatches[1].rm_eo - hmatches[1].rm_so);
        request.key[hmatches[1].rm_eo - hmatches[1].rm_so] = '\0';
        //printf("key: %s\n", request.key);

        //parsing value
        if (strncmp(header_start, "Content-Length", 14) == 0) {
            request.value = strtol(header_start + hmatches[2].rm_so, NULL, 10);
            //printf("value: %d\n", request.value);
        }

        //Moves to next header
        header_start = strstr(header_start, "\r\n");
        //printf("%s\n",header_start);
        if (header_start) {
            if (strncmp(header_start, "\r\n\r\n", 4) == 0) {
                //printf("I found the end!\n");
                break;
            }
            header_start += 2;
        }
	free(request.key);
        i++;
    }

    //parse message (if there is one)
    char *body = strstr(buffer, "\r\n\r\n");
    int offset = body - buffer + 4;
    if (body != NULL) {
        body += 4; //skips \r\n\r\n pattern
        request.message = body;
        request.remaining_bytes = bytes_read - offset;
        //  printf("Message: %s\n", body);
    }
    //printf("method: %s\n", request.method);
    //printf("version: %s\n", request.version);
    //TODO: free variables appropriately (strstr may allocate)
    regfree(&regex);
    return request;
}

//function that will process connections using the parsed request
void process_request(struct Request *request, int fd) {
    //printf("I'm in process_request!\n");
    //printf("HTTP version: %s\n", request->version);
    if (request->version == NULL || request->method == NULL) {
        dprintf(fd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
        return;
    }

    if (strcmp(request->version, "HTTP/1.1") != 0) {
        dprintf(fd, "HTTP/1.1 505 Version Not Supported\r\nContent-Length: 22\r\n\r\nVersion Not "
                    "Supported\n");
        return;
    }
    //printf("Get or put?\n");
    if (strncmp(request->method, "GET", 3) == 0) {
        //printf("Called a GET request!\n");
        int status_code = get_request(request);
        if (status_code == 200) {
            //printf("request message in process: %s\n", request->message);
            //printf("Request message: %s\n", request->message);
            dprintf(fd, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n", request->value);
            write_n_bytes(fd, request->message, request->value);
            //;
            return;
        } else if (status_code == 404) {
            dprintf(fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n");
            return;
        } else if (status_code == 403) {
            dprintf(fd, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n");
            return;
        } else if (status_code == 500) {
            dprintf(fd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal "
                        "Server Error\n");
            return;
        } else {
            dprintf(
                fd, "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n");
            return;
        }

    } else if (strncmp(request->method, "PUT", 3) == 0) {
        int status_code = put_request(request, fd);
        if (request->value == 0) {
            dprintf(fd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
            return;
        }

        if (status_code == 200) {
            ////printf("in if statement 200\n");
            dprintf(fd, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n");
            //;
            return;
        } else if (status_code == 201) {
            ////printf("in if statement 201\n");
            dprintf(fd, "HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\nCreated\n");
            printf("Returning to main!\n");
            return;
        } else if (status_code == 500) {
            dprintf(fd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal "
                        "Server Error\n");
            //;
            return;
        } else if (status_code == 403) {
            dprintf(fd, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n");
            //;
            return;
        } else {
            dprintf(
                fd, "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n");
            //;
            return;
        }
    } else {
        dprintf(fd, "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n");
        //;
        return;
    }
}

int get_request(struct Request *request) {
    //char buffer[4096];
    //int bytes_read;
    int status_code = 0;
    //printf("I'm in get_request!\n");

    if (access(request->uri, F_OK) == 0) {
        struct stat s;
        if (stat(request->uri, &s) == 0 && S_ISDIR(s.st_mode)) {
            status_code = 403;
            return status_code;
        }
        status_code = 200;
        //printf("get file exists!\n");
    } else {
        //printf("get file doesn't exist!\n");
        //if the file doesn't exist
        if (errno == ENOENT) {
            status_code = 404;
            return status_code;
        } //if the file is forbidden
        else if (errno == EACCES) {
            status_code = 403;
            return status_code;
        } else {
            status_code = 500;
            return status_code;
        }
    }
    int fd = open(request->uri, O_RDONLY);
    if (fd == -1) {
        status_code = 500;
        close(fd);
        return status_code;
    }

    //request->message = malloc(request->value + 1);
    if (!request->message) {
        close(fd);
        status_code = 500;
        return status_code;
    }
    //printf("value: %d\n", request->value);

    //writes contents of opened file to buffer
    //read_n_bytes(fd, buffer, request->value);
    struct stat s;
    fstat(fd, &s);
    request->value = s.st_size;
    //printf("Expected file size: %d\n", request->value);

    request->message = malloc(request->value + 1);
    //printf("about to pass bytes\n");

    int bytes_read = read_n_bytes(fd, request->message, request->value);
    //printf("Total bytes read: %d\n", bytes_read);
    //printf("In get: request message = %s\n", request->message);
    if (bytes_read == -1) {
        status_code = 500;
        close(fd);
        return status_code;
    }

    close(fd);
    return status_code;
}

//handles PUT request
int put_request(struct Request *request, int fd) {
    int status_code = 0;
    //char buff[4096];
    //TODO: include code to handle status codes (200 and 201)

    struct stat s;
    if (stat(request->uri, &s) == 0) {
        if (S_ISDIR(s.st_mode)) {
            //printf("I'm a directory!\n");
            status_code = 403;
            //;
            return status_code;
        }
    }

    if (access(request->uri, F_OK) == 0) {
        //file was accessed
        status_code = 200;

    } else {
        //file was not accessed, need to create
        status_code = 201;
    }

    //creat() will open an existing file and make a new one if it doesn't exist
    int f = creat(request->uri, 0666);

    if (f == -1) {
        if (errno == EACCES) {
            status_code = 403;
            return status_code;
        } else {
            status_code = 500;
            return status_code;
        }
    }
    //TODO: error handling for when there are more or less bytes than the given content-length
    printf("Remaining bytes: %d\n", request->remaining_bytes);
    int bytes_written = write_n_bytes(f, request->message, request->remaining_bytes);
    if (bytes_written == -1) {
        status_code = 500;
        close(f);
        return status_code;
    }
    printf("Bytes written: %d\n", bytes_written);

    //int total_written = request->value - request->remaining_bytes;
    int total_written = request->remaining_bytes;
    char tmpBuff[4096];

    while (total_written < request->value) {
        int bytes_to_read
            = (request->value - total_written < 4096) ? (request->value - total_written) : 4096;
        int bytes_read = read_n_bytes(fd, tmpBuff, bytes_to_read);

        if (bytes_read == -1) {
            status_code = 500;
            close(f);
            return status_code;
        }
        if (bytes_read == 0) {
            break;
        }
        bytes_written = write_n_bytes(f, tmpBuff, bytes_read);
        if (bytes_written == -1) {
            status_code = 500;
            close(f);
            return status_code;
        }
        total_written += bytes_read;
	printf("Total Written: %d, Expected: %d\n", total_written, request->value);

    }

    //     //reads rest of bytes
    //     int bytes_read = read_n_bytes(fd, tmpBuff, total_written);
    //     if(bytes_read == -1){
    //     	status_code = 500;
    //     	return status_code;
    //     }
    //     //writes rest of bytes
    //     bytes_written = write_n_bytes(f, tmpBuff, bytes_read);
    //     if(bytes_written == -1){
    //     	status_code = 500;
    //     	return status_code;
    //     }
    printf("Closing f in put!\n");
    close(f);
    printf("F has been closed\n");
    return status_code;
}

void freeRequest(struct Request *request) {
    if (request->method != NULL) {
        free(request->method);
    }
    if (request->uri != NULL) {
        free(request->uri);
    }

    if (request->version != NULL) {
        free(request->version);
    }

    if (request->key != NULL) {
        free(request->key);
    }

//    if (request->message != NULL) {
//        free(request->message);
//    }
}
