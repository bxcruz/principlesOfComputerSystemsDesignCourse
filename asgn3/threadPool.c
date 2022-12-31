#include "threadPool.h"
#include <stdlib.h>
#define EMPTY_QUEUE        -86
#define SUCCESS            200
#define MAX_QUEUE_SIZE     512 //2^9
#define BLOCKING_CONNFD    0
#define NONBLOCKING_CONNFD 1

node_t *head = NULL;
node_t *tail = NULL;
int size = 0;

int getQueueSize() {
    return size;
}

void enqueue(int clientSocket) {
    node_t *newNode = malloc(sizeof(node_t));
    size++;
    newNode->clientSocket = clientSocket;
    newNode->next = NULL;
    if (tail == NULL) {
        head = newNode;
    } else {
        tail->next = newNode;
    }
    tail = newNode;
}

int dequeue() {
    if (head == NULL) {
        return EMPTY_QUEUE;
    } else {
        size--;
        int result = head->clientSocket;
        node_t *temp = head;
        head = head->next;
        if (head == NULL) {
            tail = NULL;
        }
        free(temp);
        return result;
    }
}
