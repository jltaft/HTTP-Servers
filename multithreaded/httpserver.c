//
// ------------------------------------
// Jennifer Taft
// httpserver.c
// ------------------------------------
//

#include "listener_socket.h"
#include "connection.h"
#include "response.h"
#include "request.h"
#include "queue.h"
#include "rwlock.h"
#include "hashmap.h"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/stat.h>

//-------------Connection handling functions
void handle_connection(int);

void handle_get(conn_t *);
void handle_put(conn_t *);
void handle_unsupported(conn_t *);
void *worker_thread(void *arg);

//-------------Global Variables
queue_t *conn_queue;
HashMap rwlock_map;

//-------------Mutex for audit log
pthread_mutex_t mutex;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rwlock_mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char **argv) {
    if (argc < 2 || argc > 4) {
        warnx("wrong arguments: %s port_num", argv[0]);
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }
	
	int dasht;
	int num_threads = 4;
	char *endptr = NULL;
	//while loop to check for -t flag and perform necessary parsing
	while((dasht = getopt(argc, argv, "t:")) != -1){
		switch(dasht){
			case 't':
    	 		num_threads = (int) strtoull(optarg, &endptr, 10);
    	 		if(num_threads < 0){
    	 			warnx("invalid thread number: %s", argv[1]);
        			return EXIT_FAILURE;
    	 		}
    	 		break;
    	 	default:
    	 		break;	
		}
	}
	//block of code will account for "optional" case and "default" case
	//through using optind which gets next argument
    size_t port = (size_t) strtoull(argv[optind], &endptr, 10);
    if (endptr && *endptr != '\0') {
        warnx("invalid port number: %s", argv[optind]);
        return EXIT_FAILURE;
	}
	
    signal(SIGPIPE, SIG_IGN);
    //creates the connection queue
    conn_queue = queue_new(num_threads);
    hashmap_init(&rwlock_map);
    //initializes mutex
    pthread_mutex_init(&mutex, NULL);
    //initializes rwlock

    Listener_Socket_t *sock = ls_new(port);
    if (!sock) {
      warnx("cannot open socket");
      return EXIT_FAILURE;
    }

	//for loop creates worker thread pool
	pthread_t threads[num_threads]; //might have to malloc
	for(int i =0; i < num_threads; i++){
		pthread_create(&(threads[i]), NULL, worker_thread, NULL);
	}
	
	//-------------DISPATCHER LOOP
	//accepts connections and pushes to queue
    while (1) {
        int connfd = ls_accept(sock);
        queue_push(conn_queue, (void*)(intptr_t)connfd);
    }
    ls_delete(&sock);

    return EXIT_SUCCESS;
}

//-------------Worker Thread
void *worker_thread(void *arg){
	(void)arg;
	while(1){
		uintptr_t connfd = -1;
		queue_pop(conn_queue, (void**) &connfd);
		handle_connection(connfd);
	}
	return NULL;
}

void handle_connection(int connfd) {

    conn_t *conn = conn_new(connfd);

    const Response_t *res = conn_parse(conn);

    if (res != NULL) {
        conn_send_response(conn, res);
        pthread_mutex_lock(&mutex);
        char *id = conn_get_header(conn, "Request-Id");
        if(id == NULL){
			id = "0";
		}
        fprintf(stderr, "UNKNOWN,%s,200,%s\n", conn_get_uri(conn), id);
        pthread_mutex_unlock(&mutex);
        
    } else {
      printf("%s", conn_str(conn));
        const Request_t *req = conn_get_request(conn);
        if (req == &REQUEST_PUT) {
            handle_put(conn);
        }
        if (req == &REQUEST_GET) {
            handle_get(conn);
        }
		if(req == &REQUEST_UNSUPPORTED){
	    	handle_unsupported(conn);
		}
		//conn_send_response(conn, &RESPONSE_BAD_REQUEST);
		//conn_send_response(conn, &RESPONSE_NOT_IMPLEMENTED);
	// add cases for other types of requests here
    }
    conn_delete(&conn);
    close(connfd);
}

void handle_put(conn_t *conn) {

    char *uri = conn_get_uri(conn);
    
    const Response_t *res = NULL;
    
    //look for uri in hashmap and then writer_lock
    rwlock_t *file_rwlock = hashmap_function(&rwlock_map, uri);
    if (!file_rwlock) {
        res = &RESPONSE_INTERNAL_SERVER_ERROR;
        conn_send_response(conn, res);
        return;
    }
    writer_lock(file_rwlock);
    
    bool existed = access(uri, F_OK) == 0;

    int fd = open(uri, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    
    if (fd < 0){
    	if(errno == EACCES || errno == EISDIR || errno == ENOENT){
    		res = &RESPONSE_FORBIDDEN;
    	} else{
    		res = &RESPONSE_INTERNAL_SERVER_ERROR; 
    	}
    	conn_send_response(conn, res);
    	pthread_mutex_lock(&mutex);
		char *id = conn_get_header(conn, "Request-Id");
		if(id == NULL){
			id = "0";
		}
    	fprintf(stderr, "PUT,%s,%d,%s\n", conn_get_uri(conn), (res == &RESPONSE_FORBIDDEN) ? 403 : 500, id);
    	pthread_mutex_unlock(&mutex);
    	
    	writer_unlock(file_rwlock);
    	return;
    }
    //writes info from conn to the uri
    res = conn_recv_file(conn, fd);
    close(fd);
       // check to see if it was OK.
    if (res == NULL) {
        if (existed) {
            res = &RESPONSE_OK;
        } else{
            res = &RESPONSE_CREATED;
        } 
    }  
    conn_send_response(conn, res);
    
    pthread_mutex_lock(&mutex);
    char *id = conn_get_header(conn, "Request-Id");
    if(id == NULL){
		id = "0";
	}
   	fprintf(stderr, "PUT,%s,%d,%s\n", conn_get_uri(conn),(res == &RESPONSE_OK) ? 200 : 201, id);
    pthread_mutex_unlock(&mutex);
    
    writer_unlock(file_rwlock);
    return;
}

void handle_unsupported(conn_t *conn){
	conn_send_response(conn, &RESPONSE_NOT_IMPLEMENTED);
	
	pthread_mutex_lock(&mutex);
	char *id = conn_get_header(conn, "Request-Id");
	if(id == NULL){
		id = "0";
	}
    fprintf(stderr, "UNSUPPORTED,%s,501,%s\n", conn_get_uri(conn), id);
    pthread_mutex_unlock(&mutex);
}

void handle_get(conn_t *conn){
	char *uri = conn_get_uri(conn);
    
    const Response_t *res = NULL; 
    
    //look for uri in hashmap and then writer_lock
    rwlock_t *file_rwlock = hashmap_function(&rwlock_map, uri);
    if (!file_rwlock) {
        res = &RESPONSE_INTERNAL_SERVER_ERROR;
        conn_send_response(conn, res);
        return;
    }
    
    reader_lock(file_rwlock);
    
    int fd = open(uri, O_RDONLY);
    
    if (fd < 0){
    	if(errno == EACCES || errno == EISDIR){
    		res = &RESPONSE_FORBIDDEN;
		} else if(errno == ENOENT){
			res = &RESPONSE_NOT_FOUND;
		} else{
    		res = &RESPONSE_INTERNAL_SERVER_ERROR; 
    		
    	}
    	conn_send_response(conn, res);
    	
    	pthread_mutex_lock(&mutex);
    	char *id = conn_get_header(conn, "Request-Id");
    	if(id == NULL){
			id = "0";
		}
    	//fprintf accounts for 403, 404, 500 status codes
    	fprintf(stderr, "GET,%s,%d,%s\n", uri, (res == &RESPONSE_FORBIDDEN) ? 403 : (res == &RESPONSE_NOT_FOUND) ? 404 : 500, id);
   		pthread_mutex_unlock(&mutex);
   		
   		reader_unlock(file_rwlock);		
   		return;
    }
    
    //finds how many bytes to read from header
	struct stat value;
	fstat(fd, &value);
	int size = value.st_size;
	//check for directory	
	if(S_ISDIR(value.st_mode)){
		res = &RESPONSE_FORBIDDEN;
		conn_send_response(conn, res);
		
		char *id = conn_get_header(conn, "Request-Id");
		if(id == NULL){
			id = "0";
		}
		fprintf(stderr, "GET,%s,403,%s\n", uri, id);
		pthread_mutex_unlock(&mutex);
		
		reader_unlock(file_rwlock);
		close(fd);
		return;
	}
	//gets message body from the file and sends it to connection
	res = conn_send_file(conn, fd, size);
    close(fd);
    
    if(res == NULL){
    	res = &RESPONSE_OK;
    }
	reader_unlock(file_rwlock);
    

	char *id = conn_get_header(conn, "Request-Id");
	if(id == NULL){
		id = "0";
	}
	fprintf(stderr, "GET,%s,200,%s\n", uri, id);
	pthread_mutex_unlock(&mutex);

	return;
}

