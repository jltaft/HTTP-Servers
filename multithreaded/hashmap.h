//
//Implementation of hashmap.c
//jltaft
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "rwlock.h"

#define HASH_SIZE 128 //might have to change t

typedef struct HashNode {
	//uri
    char *key;             
    //will be used per file
    rwlock_t *lock;       
    struct HashNode *next; 
} HashNode;

typedef struct {
    HashNode *buckets[HASH_SIZE]; 
    pthread_mutex_t mutex;        
} HashMap;

unsigned int hash_function(const char *key) {
    unsigned int hash = 0;
    while (*key) {
        hash = (hash * 31) + (*key++); 
    }
    return hash % HASH_SIZE;
}

void hashmap_init(HashMap *map) {
    memset(map->buckets, 0, sizeof(map->buckets));
    pthread_mutex_init(&map->mutex, NULL);
}

rwlock_t *hashmap_function(HashMap *map, const char *key) {
    unsigned int index = hash_function(key);
    
    pthread_mutex_lock(&map->mutex);  // Protect hash map from concurrent access

    HashNode *node = map->buckets[index];
    while (node) {
        if (strcmp(node->key, key) == 0) {  // If key exists, return its lock
            pthread_mutex_unlock(&map->mutex);
            return node->lock;
        }
        node = node->next;
    }

    // Key not found, create a new entry
    node = malloc(sizeof(HashNode));
    if (!node) {
        pthread_mutex_unlock(&map->mutex);
        return NULL; // Memory allocation failure
    }
    node->key = strdup(key);
    node->lock = rwlock_new(READERS, 0); // Create rwlock
    node->next = map->buckets[index];
    map->buckets[index] = node;

    pthread_mutex_unlock(&map->mutex);
    return node->lock;
}

void hashmap_remove(HashMap *map, const char *key) {
    unsigned int index = hash_function(key);

    pthread_mutex_lock(&map->mutex);

    HashNode *node = map->buckets[index];
    HashNode *prev = NULL;

    while (node) {
        if (strcmp(node->key, key) == 0) {  // Found the key
            if (prev) {
                prev->next = node->next;
            } else {
                map->buckets[index] = node->next;
            }
            free(node->key);
            rwlock_delete(&(node->lock));
            free(node);
            break;
        }
        prev = node;
        node = node->next;
    }

    pthread_mutex_unlock(&map->mutex);
}
