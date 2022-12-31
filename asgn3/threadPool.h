#ifndef THREADPOOL_H_
#define THREADPOOL_H_
//#define MAXLENGTH 2048

struct node {
    struct node *next;
    int clientSocket;
};

typedef struct node node_t;

int getQueueSize();

void enqueue(int clientSocket);

int dequeue();

#endif
