//
// ------------------------------------
// Jennifer Taft
// httpproxy.c
// ------------------------------------
//

#include "client_socket.h"
#include "iowrapper.h"
#include "listener_socket.h"
#include "prequest.h"
#include "a5protocol.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/file.h>
#include <sys/stat.h>
#include <regex.h>

// globals:
Listener_Socket_t *sock = NULL;
int n = 0;

typedef struct CacheEntry {
    char *host;
    char *uri;
    int port;
    char *headers;
    char *body;
    size_t headers_size;
    size_t body_size;
    struct CacheEntry *prev, *next;
} CacheEntry_t;

typedef struct {
    CacheEntry_t *head, *tail;
    int count;
    int is_lru;
} Cache_t;

Cache_t cache = { .head = NULL, .tail = NULL, .count = 0, .is_lru = 0 };

CacheEntry_t *cache_lookup(const char *host, int port, const char *uri) {
    CacheEntry_t *curr = cache.head;
    while (curr) {
        if (strcmp(curr->host, host) == 0 && curr->port == port && strcmp(curr->uri, uri) == 0) {
            if (cache.is_lru && curr != cache.head) { // Move to front if LRU
                if (curr->prev)
                    curr->prev->next = curr->next;
                if (curr->next)
                    curr->next->prev = curr->prev;
                if (cache.tail == curr)
                    cache.tail = curr->prev;

                curr->next = cache.head;
                curr->prev = NULL;
                if (cache.head)
                    cache.head->prev = curr;
                cache.head = curr;
            }
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

void cache_evict(void) {
    if (cache.count == 0)
        return;
    // LRU removes tail, FIFO removes head
    CacheEntry_t *evict = (cache.is_lru) ? cache.tail : cache.head;

    if (evict->prev)
        evict->prev->next = evict->next;
    if (evict->next)
        evict->next->prev = evict->prev;

    if (cache.head == evict)
        cache.head = evict->next;
    if (cache.tail == evict)
        cache.tail = evict->prev;

    free(evict->host);
    free(evict->uri);
    free(evict->headers);
    free(evict->body);
    free(evict);

    cache.count--;
}

void cache_store(const char *host, int port, const char *uri, const char *response, size_t size) {
    if (n == 0 || size > MAX_CACHE_ENTRY)
        return;

    while (cache.count >= n)
        cache_evict();

    char *header_end = strstr(response, "\r\n\r\n");
    if (!header_end)
        return;

    size_t status_line_end = strchr(response, '\n') - response + 1;
    size_t header_size = header_end - response + 4;
    size_t body_size = size - header_size;

    CacheEntry_t *new_entry = malloc(sizeof(CacheEntry_t));
    new_entry->host = strdup(host);
    new_entry->port = port;
    new_entry->uri = strdup(uri);

    // Store headers and body separately
    new_entry->headers = malloc(header_size + strlen(CACHED_HEADER) + 4);
    memcpy(new_entry->headers, response, status_line_end);
    memcpy(new_entry->headers + status_line_end, CACHED_HEADER "\r\n", strlen(CACHED_HEADER) + 2);
    memcpy(new_entry->headers + status_line_end + strlen(CACHED_HEADER) + 2,
        response + status_line_end, header_size - status_line_end);

    new_entry->headers_size = header_size + strlen(CACHED_HEADER) + 2;
    new_entry->body = malloc(body_size);
    memcpy(new_entry->body, response + header_size, body_size);
    new_entry->body_size = body_size;

    if (cache.is_lru) {
        //insert at head
        new_entry->prev = NULL;
        new_entry->next = cache.head;
        if (cache.head)
            cache.head->prev = new_entry;
        cache.head = new_entry;
        if (!cache.tail)
            cache.tail = new_entry;
    } else {
        //insert at tail, FIFO
        new_entry->next = NULL;
        new_entry->prev = cache.tail;
        if (cache.tail)
            cache.tail->next = new_entry;
        cache.tail = new_entry;
        if (!cache.head)
            cache.head = new_entry;
    }
    cache.count++;
}

void handle_connection(uintptr_t);
int connect_to_server(const char *host, int port);
CacheEntry_t *cache_lookup(const char *host, int port, const char *uri);
void cache_store(const char *host, int port, const char *uri, const char *response, size_t size);

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <port> <mode> <n>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    if (port < 1 || port > 65535) {
        fprintf(stderr, "Invalid Argument\n");
        return EXIT_FAILURE;
    }

    if (strcmp(argv[2], "LRU") == 0) {
        cache.is_lru = 1;
    } else if (strcmp(argv[2], "FIFO") != 0) {
        fprintf(stderr, "Invalid mode\n");
        return EXIT_FAILURE;
    }

    n = atoi(argv[3]);
    if (n < 0) {
        fprintf(stderr, "Invalid cache size\n");
        return EXIT_FAILURE;
    }

    sock = ls_new(port);
    if (!sock) {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }

    while (1) {
        uintptr_t connfd = ls_accept(sock);
        assert(connfd > 0);
        handle_connection(connfd);
    }

    ls_delete(&sock);
    return EXIT_SUCCESS;
}

void handle_connection(uintptr_t connfd) {
    Prequest_t *preq = prequest_new(connfd);
    if (!preq) {
        fprintf(stderr, "Bad request from %lu\n", connfd);
        return;
    }

    char *host = prequest_get_host(preq);
    char *uri = prequest_get_uri(preq);
    int32_t port = prequest_get_port(preq);

    char key[MAX_URI_SIZE + MAX_HOST_SIZE];
    snprintf(key, sizeof(key), "%s:%d%s", host, port, uri);

    CacheEntry_t *entry = cache_lookup(host, port, uri);
    if (entry) {
        // Send cached headers
        write_n_bytes(connfd, entry->headers, entry->headers_size);
        // Send cached body
        write_n_bytes(connfd, entry->body, entry->body_size);

        prequest_delete(&preq);
        close(connfd);
        return;
    }

    int client_sock = cs_new(host, port);
    if (client_sock < 0) {
        fprintf(stderr, "Cannot connect to host %s\n", host);
        prequest_delete(&preq);
        return;
    }

    // Send GET request
    char request[512];
    int req
        = snprintf(request, sizeof(request), "GET /%s HTTP/1.1\r\nConnection: Close\r\n\r\n", uri);
    write_n_bytes(client_sock, request, req);

    // Read response dynamically
    size_t total_size = 0;
    size_t buffer_size = MAX_CACHE_ENTRY;
    char *full_response = malloc(buffer_size);
    if (!full_response) {
        fprintf(stderr, "Memory allocation failed\n");
        close(client_sock);
        prequest_delete(&preq);
        close(connfd);
        return;
    }

    int bytes_read;
    while ((bytes_read
               = read_n_bytes(client_sock, full_response + total_size, buffer_size - total_size))
           > 0) {
        total_size += bytes_read;

        // Resize buffer if needed
        if (total_size >= buffer_size - 1) {
            buffer_size *= 2;
            char *new_buffer = realloc(full_response, buffer_size);
            if (!new_buffer) {
                fprintf(stderr, "Memory reallocation failed\n");
                free(full_response);
                close(client_sock);
                prequest_delete(&preq);
                close(connfd);
                return;
            }
            full_response = new_buffer;
        }

        // Forward data to client immediately
        write_n_bytes(connfd, full_response + total_size - bytes_read, bytes_read);
    }

    // Store the full response in cache
    if (total_size > 0) {
        cache_store(host, port, uri, full_response, total_size);
    }

    free(full_response);
    close(client_sock);
    prequest_delete(&preq);
    close(connfd);
}
