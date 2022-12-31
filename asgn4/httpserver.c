#define _GNU_SOURCE
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/file.h>

#include <pthread.h>

#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include "threadPool.h"

#define UNUSED(x) (void) (x)

#define MAXLENGTH       2048
#define GET_CONST       10
#define PUT_CONST       11
#define APPEND_CONST    12
#define RES_OK          200
#define RES_CREATED     201
#define RES_NOT_FOUND   404
#define RES_INTERNAL_SE 500

#define RES_BAD_REQUEST 400
#define RES_FORBIDDEN   403
#define RES_NOT_IMPL    501

#define OPTIONS              "t:l:"
#define BUF_SIZE             4096
#define DEFAULT_THREAD_COUNT 4
#define INITVAL              -1337
#define MAX_QUEUE_SIZE       1000 //2^9
static FILE *logfile;
#define LOG(...) fprintf(logfile, __VA_ARGS__);

static char init[10] = "init char*";
void *threadFunction(void *arg);
pthread_mutex_t myMutex = PTHREAD_MUTEX_INITIALIZER; //lock and unlock dequeue
pthread_mutex_t closeConnMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t logLock = PTHREAD_MUTEX_INITIALIZER;
pthread_rwlock_t readLock;
pthread_rwlock_t writeLock;

pthread_cond_t canWork = PTHREAD_COND_INITIALIZER;
pthread_cond_t queueFull = PTHREAD_COND_INITIALIZER;

//int QueueCapacityCount = 0;

// Converts a string to an 16 bits unsigned integer.
// Returns 0 if the string is malformed or out of the range.
static size_t strtouint16(char number[]) {
    char *last;
    long num = strtol(number, &last, 10);
    if (num <= 0 || num > UINT16_MAX || *last != '\0') {
        return 0;
    }
    return num;
}

// Creates a socket for listening for connections.
// Closes the program and prints an error message on error.
static int create_listen_socket(uint16_t port) {
    struct sockaddr_in addr;
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        err(EXIT_FAILURE, "socket error");
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htons(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *) &addr, sizeof addr) < 0) {
        err(EXIT_FAILURE, "bind error");
    }
    if (listen(listenfd, 128) < 0) {
        err(EXIT_FAILURE, "listen error");
    }
    return listenfd;
}

int checkForASCII(char c) {
    //if alphanumeric or stuff like ! ' " # $
    if (isprint(c) != 0 || isalpha(c) != 0 || isalnum(c) != 0 || isblank(c) != 0 || c == '\r'
        || c == '\n') {
        return 1;
    }
    return 0;
}

void sendOk(int connfd, char *buff) {
    //200, non get
    strcpy(buff, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n");
    send(connfd, buff, strlen(buff), 0);
}
void sendCreated(int connfd, char *buff) {
    //201
    strcpy(buff, "HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\nCreated\n");
    send(connfd, buff, strlen(buff), 0);
}
void sendBadRequest(int connfd, char *buff) {
    //400
    strcpy(buff, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
    send(connfd, buff, strlen(buff), 0);
}
void sendForbidden(int connfd, char *buff) {
    //403
    strcpy(buff, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n");
    send(connfd, buff, strlen(buff), 0);
}
void sendNotFound(int connfd, char *buff) {
    //404
    strcpy(buff, "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n");
    send(connfd, buff, strlen(buff), 0);
}
void sendInternalServerError(int connfd, char *buff) {
    //500
    strcpy(buff, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: "
                 "22\r\n\r\nInternal Server Error\n");
    send(connfd, buff, strlen(buff), 0);
}
void sendNotImplemented(int connfd, char *buff) {
    //501
    strcpy(buff, "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n");
    send(connfd, buff, strlen(buff), 0);
}

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

int processGet(int connfd, char *filePath) {
    struct stat st;
    //pthread_rwlock_rdlock(&readLock);
    int getFd = open(filePath, O_RDONLY | F_OK);
    //flock(getFd, LOCK_SH);

    char fileSizeStr[1024] = "+";
    memset(fileSizeStr, 0, 1024);
    char resBuff[MAXLENGTH] = "+";
    memset(resBuff, 0, MAXLENGTH);

    if (getFd < 0) {
        //any errors, get out of the process immediately,
        //nothing to keep reading if we error.

        //warn("<%d>", errno);
        if (errno == EACCES) {
            //printf("\n<EACCES triggered>\n");
            //flock(getFd, LOCK_UN);
            close(getFd);
            //pthread_rwlock_unlock(&readLock);
            sendForbidden(connfd, resBuff);
            return RES_FORBIDDEN;
        }
        if (errno == ENOENT) {
            //flock(getFd, LOCK_UN);
            close(getFd);
            //pthread_rwlock_rdlock(&readLock);
            sendNotFound(connfd, resBuff);

            return RES_NOT_FOUND;
        } else {
            //warn("why");
            //flock(getFd, LOCK_UN);
            close(getFd);
            //pthread_rwlock_unlock(&readLock);
            sendInternalServerError(connfd, resBuff);
            return RES_INTERNAL_SE;
        }

    } else {
        fstat(getFd, &st);
        int fileSize = st.st_size;
        char chunk[BUF_SIZE] = "+";
        memset(chunk, 0, BUF_SIZE);
        int bytesRead;
        int totalGotten = 0;

        strcpy(resBuff, "HTTP/1.1 200 OK\r\nContent-Length: ");
        sprintf(fileSizeStr, "%d", fileSize);
        strcat(resBuff, fileSizeStr);
        strcat(resBuff, "\r\n\r\n");
        send(connfd, resBuff, strlen(resBuff), 0);

        while (totalGotten < fileSize) {
            bytesRead = read(getFd, chunk, BUF_SIZE);
            if (bytesRead < BUF_SIZE && (bytesRead + totalGotten) == fileSize) {
                totalGotten += bytesRead;
                send(connfd, chunk, bytesRead, 0);
                break;
            }
            send(connfd, chunk, bytesRead, 0);
            totalGotten += bytesRead;
        }
        //flock(getFd, LOCK_UN);
        close(getFd);
        //pthread_rwlock_unlock(&readLock);
        return RES_OK;
    }
    //flock(getFd, LOCK_UN);
    close(getFd);
    //pthread_rwlock_unlock(&readLock);
    return RES_INTERNAL_SE;
}

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

int processPut(int connfd, char *filePath, int contLen, char *body) {
    int putFd;
    int created = 0;
    if (access(filePath, F_OK) != -1) {
        created = 0;
    } else {
        created = 1;
    }

    //consolidate the open call and add reason code
    //pthread_rwlock_wrlock(&writeLock);
    putFd = open(
        filePath, O_RDWR | O_CREAT | O_TRUNC, 0644); // one call instead of 2 alternate calls?
    //flock(putFd, LOCK_EX);
    if (putFd < 0) {
        //stop processing and exit on any error

        char resBuff[MAXLENGTH] = "+";
        memset(resBuff, 0, MAXLENGTH);
        if (errno == EACCES) {
            //flock(putFd, LOCK_UN);
            close(putFd);
            //pthread_rwlock_unlock(&writeLock);
            sendForbidden(connfd, resBuff);
            return RES_FORBIDDEN;
        }
        if (errno != 0) {
            //flock(putFd, LOCK_UN);
            close(putFd);
            //pthread_rwlock_unlock(&writeLock);
            sendInternalServerError(connfd, resBuff);
            return RES_INTERNAL_SE;
        }
    } else {

        write(putFd, body, contLen);
        //flock(putFd, LOCK_UN);
        close(putFd);
        //pthread_rwlock_unlock(&writeLock);
        if (created == 1) {
            //send response after processing
            return RES_CREATED;
        }
        if (created == 0) {
            //send response after processing
            return RES_OK;
        }
    }

    return RES_INTERNAL_SE;
}

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

int processPutFile(
    int connfd, char *filePath, int contLen, char *firstBodyChunk, int firstBodyChunkVal) {

    int putFd;
    int created = 0;

    if (access(filePath, F_OK) != -1) {
        created = 0;
    } else {
        created = 1;
    }

    //consolidate the open call and add reason code
    //pthread_rwlock_wrlock(&writeLock);
    putFd = open(
        filePath, O_RDWR | O_CREAT | O_TRUNC, 0644); // one call instead of 2 alternate calls?
    //flock(putFd, LOCK_EX);
    if (putFd < 0) {
        char resBuff[MAXLENGTH] = "+";
        memset(resBuff, 0, MAXLENGTH);
        //stop processing and exit on any error
        if (errno == EACCES) {
            //flock(putFd, LOCK_UN);
            close(putFd);
            //pthread_rwlock_unlock(&writeLock);
            sendForbidden(connfd, resBuff);
            return RES_FORBIDDEN;
        }
        if (errno != 0) {
            //flock(putFd, LOCK_UN);
            close(putFd);
            //pthread_rwlock_unlock(&writeLock);
            sendInternalServerError(connfd, resBuff);
            return RES_INTERNAL_SE;
        }
    } else {
        //first write of first chunk from very first recv()
        if (firstBodyChunkVal > 0) {
            write(putFd, firstBodyChunk, firstBodyChunkVal);
        }

        //remaining chunks (if there are any remaining)
        //if contentLength > than body AND all of body
        //is contained in very first recv, then just
        //write # bytes in body.
        int bytesRead = 0;
        int totalToReceive = 0;
        char chunk[BUF_SIZE] = "+";
        memset(chunk, 0, BUF_SIZE);
        while (totalToReceive < contLen) {
            bytesRead = recv(connfd, chunk, BUF_SIZE, 0);
            if (bytesRead == 0) {
                //warn("hi there are no more bytes to read");
                break;
            }
            //warn("hi i continue reading");
            if (bytesRead < BUF_SIZE && (bytesRead + totalToReceive) == contLen) {
                //warn("bytRR<%d>", bytesRead);
                //warn("MX - bytRR<%d>", MAXLENGTH - bytesRead);
                totalToReceive += bytesRead;
                //warn("%d", totalToReceive);
                write(putFd, chunk, bytesRead);

                break;
            }
            write(putFd, chunk, bytesRead);
            totalToReceive += bytesRead;
        }
        //flock(putFd, LOCK_UN);
        close(putFd);
        //pthread_rwlock_unlock(&writeLock);
        if (created == 1) {
            //send response after processing
            return RES_CREATED;
        }
        if (created == 0) {
            //send response after processing
            return RES_OK;
        }
    }

    return RES_INTERNAL_SE;
}

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

int processAppend(int connfd, char *filePath, int contLen, char *body) {
    char resBuff[MAXLENGTH] = "+";
    memset(resBuff, 0, MAXLENGTH);
    //pthread_rwlock_wrlock(&writeLock);
    int appendFd = open(filePath, O_WRONLY | O_APPEND, 0644);
    //flock(appendFd, LOCK_EX);
    if (appendFd < 0) {
        //send error immediately. do not continue processing
        if (errno == EACCES) {
            //printf("\n<EACCES triggered>\n");
            //flock(appendFd, LOCK_UN);
            close(appendFd);
            //pthread_rwlock_unlock(&writeLock);
            sendForbidden(connfd, resBuff);
            return RES_FORBIDDEN;
        }
        if (errno == ENOENT) {
            //flock(appendFd, LOCK_UN);
            close(appendFd);
            //pthread_rwlock_unlock(&writeLock);
            sendNotFound(connfd, resBuff);
            return RES_NOT_FOUND;

        } else {
            //flock(appendFd, LOCK_UN);
            close(appendFd);
            //pthread_rwlock_unlock(&writeLock);
            sendInternalServerError(connfd, resBuff);
            return RES_INTERNAL_SE;
        }
    }

    int location = lseek(appendFd, 0, SEEK_END);
    printf("\nlocation <%d>", location);
    write(appendFd, body, contLen);
    //flock(appendFd, LOCK_UN);
    close(appendFd);
    //pthread_rwlock_unlock(&writeLock);
    //send OK outside once finished APPENDing
    return RES_OK;
}

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

int processAppendFile(
    int connfd, char *filePath, int contLen, char *firstBodyChunk, int firstBodyChunkVal) {
    char resBuff[MAXLENGTH] = "+";
    memset(resBuff, 0, MAXLENGTH);
    //pthread_rwlock_wrlock(&writeLock);
    int appendFd = open(filePath, O_RDWR | O_APPEND, 0644);
    //flock(appendFd, LOCK_EX);
    if (appendFd < 0) {
        //send error immediately. do not continue processing
        if (errno == EACCES) {
            //printf("\n<EACCES triggered>\n");
            //flock(appendFd, LOCK_UN);
            close(appendFd);
            //pthread_rwlock_unlock(&writeLock);
            sendForbidden(connfd, resBuff);
            return RES_FORBIDDEN;
        }
        if (errno == ENOENT) {
            sendNotFound(connfd, resBuff);
            //flock(appendFd, LOCK_UN);
            close(appendFd);
            //pthread_rwlock_unlock(&writeLock);
            return RES_NOT_FOUND;

        } else {
            sendInternalServerError(connfd, resBuff);
            //flock(appendFd, LOCK_UN);
            close(appendFd);
            //pthread_rwlock_unlock(&writeLock);
            return RES_INTERNAL_SE;
        }
    }

    int location = lseek(appendFd, 0, SEEK_END);

    printf("\nlocation <%d>", location);

    //first write of first chunk from very first recv()
    if (firstBodyChunkVal > 0) {
        write(appendFd, firstBodyChunk, firstBodyChunkVal);
    }

    //remaining chunks (if there are any remaining)
    //if contentLength > than body AND all of body
    //is contained in very first recv, then just
    //write # bytes in body.
    int bytesRead = 0;
    int totalToReceive = 0;
    char chunk[BUF_SIZE] = "+";
    memset(chunk, 0, BUF_SIZE);
    while (totalToReceive < contLen) {
        bytesRead = recv(connfd, chunk, BUF_SIZE, 0);
        if (bytesRead == 0) {
            break;
        }
        if (bytesRead < BUF_SIZE && (totalToReceive + bytesRead) == contLen) {
            //warn("bytRR<%d>", bytesRead);
            //warn("MX - bytRR<%d>", MAXLENGTH - bytesRead);
            totalToReceive += bytesRead;
            //warn("%d", totalToReceive);
            write(appendFd, chunk, bytesRead);

            break;
        }
        write(appendFd, chunk, bytesRead);
        totalToReceive += bytesRead;
    }
    //flock(appendFd, LOCK_UN);
    close(appendFd);
    //pthread_rwlock_unlock(&writeLock);
    //send OK outside once finished APPENDing
    return RES_OK;
}

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

//Request format: Request-Line\r\nHeader-Field\r\n\r\n

//PUT: PUT echo -e "PUT /foo.txt HTTP/1.1\r\nContent-Length: 12\r\n\r\nHello world!" | nc -q 0 localhost 8085
//echo -e "PUT /foo.txt HTTP/1.1\r\nContent-Length:12\r\n\r\nHello world!!" | nc -q 0 localhost 8083
//echo -e "PUT /foo.txt HTTP/1.1\r\nContent-Length: 1111\r\n\r\nHello world!!" | nc localhost 8083
//PUT: curl -d "Hi Hi Hi" -X PUT http://localhost:8083/foo.txt
//(printf 'PUT /foo.txt HTTP/1.1\r\nContent-Length: SIZEINBYTES\r\n\r\n') <(some_file) | nc localhost 8082
//curl -X PUT "localhost:8081/foo11.txt" -F "file=@httpserver"

//GET FULL STRING: echo -e "GET /httpserver HTTP/1.1\r\nHost: localhost:8080\r\nUser-Agent:curl/7.68.0\r\nAccept: */*\r\n\r\n" | nc -q 0 localhost 8080
//echo -e "GET /httpserver HTTP/1.1\r\nHost: localhost:8080\r\nUser-Agent:curl/7.68.0\r\nAccept: */*\r\n" | nc -q 0 localhost 8080
//echo -e "GET /foo1.txt HTTP/1.1\r\nHost: localhost:8086\r\nUser-Agent:curl/7.68.0\r\nAccept: */*\r\n\r\n" | nc -q 0 localhost 8086
//GET: GET /foo.txt HTTP/1.1\r\n\r\n
//GET (curl): curl http://localhost:8081/httpserver -v --output -

//APPEND: echo -e "APPEND /foo.txt HTTP/1.1\r\nContent-Length: 11\r\n\r\nHello World!" | nc -q 0 localhost 8080
//echo -e "APPEND /foo.txt HTTP/1.1\r\nContent-Length: 11\r\n\r\nHell\0\0\0orld" | nc -q 0 localhost 8086

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

void handle_connection(int connfd) {
    // make the compiler not complain
    //errno = 0;
    int bytesRead = 0;
    //asgn1 headers
    char reqString[MAXLENGTH] = "+";
    memset(reqString, 0, MAXLENGTH);
    char *reqParser = init; //char ptr to only get correct fields for reqs (ignore others)
    char resBuff[MAXLENGTH] = "+"; //print responses on outer process call
    memset(resBuff, 0, MAXLENGTH);
    char method[2048] = "+"; //method string
    memset(method, 0, 2048);
    char filePath[2048] = "+"; //filePath string
    memset(filePath, 0, 2048);
    char version[2048] = "+"; //version string
    memset(version, 0, 2048);

    //asgn2 headers
    char requestIdStr[2048] = "+";
    memset(requestIdStr, 0, 2048);
    char requestIdNumStr[2048] = "+";
    memset(requestIdNumStr, 0, 2048);

    //asgn1 headers
    char contentLength[2048] = "+"; //content-length string
    memset(contentLength, 0, 2048);
    char contLenField[2048] = "+";
    memset(contLenField, 0, 2048);
    char *reqStr = init;
    char *beginStr = init;
    char *keyValCheck = init;
    char *reqIdParser = init;

    int contentLengthInt = INITVAL;
    int beforeBody = INITVAL;
    int bodyVal = INITVAL;
    int returnCode = INITVAL;
    int requestIdNum = 0;
    int requestLength = INITVAL;
    int totalBodyLeft = INITVAL;

    //char *checkRN = NULL;
    int initialReqRead = 0;

    // while (initialReqRead < MAXLENGTH) {
    //     bytesRead
    //         = recv(connfd, reqString + initialReqRead, MAXLENGTH - initialReqRead, 0 | O_NONBLOCK);
    //     if (!bytesRead) {
    //         initialReqRead += bytesRead;
    //         break;
    //     }
    //     if ((checkRN = strstr(reqString, "\r\n\r\n")) != NULL) {
    //         warn("<%s>", reqString);
    //         initialReqRead += bytesRead;
    //         break;
    //     } else if (checkRN == NULL) {
    //         warn("<<INITIAL RECV DIDN'T GUARANTEE ENTIRE REQ LINE>>");
    //         initialReqRead += bytesRead;
    //     }
    // }
    // if (bytesRead < 0) {
    //     warn("recv error");
    //     (void) connfd;
    //     return;
    //     //exit(-1);
    // }

    while (initialReqRead < MAXLENGTH) {
        bytesRead = recv(connfd, reqString + initialReqRead, MAXLENGTH - initialReqRead, 0);
        initialReqRead += bytesRead;
        if (!bytesRead) {
            break;
        }
        // if (bytesRead < 0) {
        //     warn("recv error");
        //     //(void) connfd;
        //     return;
        //     //exit(-1);
        // }
    }
    requestLength = initialReqRead; // only used when getting initial request recv()
    reqStr = reqString;
    beginStr = reqString;

    //skip this part if we got content length
    //if (headersGrabbedMinusContLen == NOT_GRABBED) {
    keyValCheck = reqString;
    int parse = 0;
    while ((keyValCheck = strstr(keyValCheck, "\r\n"))) {
        //printf("\n<%s>\n", keyValCheck);
        while (checkForASCII(keyValCheck[parse]) == 1 && keyValCheck[parse] != '\r') {
            //printf("<%c>", keyValCheck[parse]);
            parse++;
        }
        if (checkForASCII(keyValCheck[parse]) == 0) {
            sendBadRequest(connfd, resBuff);
            (void) connfd;
            return;
            //return 400;
        }
        parse = 0;
        keyValCheck += 1;
    }

    //REQUEST PARSING: METHOD, FILEPATH, VERSION (ONLY FIELDS NEEDED FOR GET)
    sscanf(reqStr, "%s[a-zA-Z]{1,8}", method); //grab method
    reqStr = strstr(reqStr, " /"); //next sscanf() acting up
    if (reqStr == NULL) {
        sendBadRequest(connfd, resBuff);
        (void) connfd;
        return;
        //return 400;
    }

    sscanf(reqStr, "%s/[a-zA-Z0-9._]{1,19} ", filePath); //grab filepath
    reqStr = strstr(reqStr, " HTTP"); //sscanf() acting up
    if (!reqStr) {
        sendBadRequest(connfd, resBuff);
        (void) connfd;
        return;
        //return 400;
    }

    for (unsigned long i = 0; i < strlen(filePath); i++) {
        if (checkForASCII(filePath[i]) == 0) {
            sendBadRequest(connfd, resBuff);
            (void) connfd;
            return;
        }
    }

    sscanf(reqStr, "%sHTTP/[0-9].[0-9]\r\n", version); //grab HTTP version

    if (strlen(method) > 8 || strlen(method) < 3 || filePath[0] != '/' || strlen(filePath) > 19
        || strlen(filePath + 1) < 1 || strcmp(version, "HTTP/1.1") != 0) {
        //warn("filePathWithSlash: <%s>, filePathW/OSlash: <%s>", filePath, filePath + 1);
        sendBadRequest(connfd, resBuff);
        (void) connfd;
        return;
        //return 400;
    }

    if (strcmp(method, "GET") != 0 && strcmp(method, "get") != 0 && strcmp(method, "PUT") != 0
        && strcmp(method, "put") != 0 && strcmp(method, "APPEND") != 0
        && strcmp(method, "append") != 0) {
        // printf("TODO: write response for 501 Not Implemented Content-Length: 15\r\n\r\nNot "
        //        "Implemented");
        sendNotImplemented(connfd, resBuff);
        (void) connfd;
        return;
        //return 501;
    }

    reqIdParser = strstr(reqStr, "Request-Id:");
    if (!reqIdParser) {
        //warn("caught null ptr! no request id");
        //warn("<%s>", reqIdParser);
        requestIdNum = 0;
    } else {
        sscanf(reqIdParser, "\r\n%s %s\r\n", requestIdStr, requestIdNumStr);
        requestIdNum = atoi(requestIdNumStr);
        reqIdParser = strstr(reqIdParser, ":");
        if (reqIdParser[1] != ' ') {
            requestIdNum = 0; //strstr returned "Request-Id:<sum # w/o a space btwn>"
        }
        //warn("rqID: <%d> rqIDStr <%s>", requestIdNum, requestIdNumStr);
    }

    //headersGrabbedMinusContLen = GRABBED;
    //}

    if (strcmp(method, "GET") == 0 || strcmp(method, "get") == 0) {
        pthread_rwlock_rdlock(&readLock);
        returnCode = processGet(connfd, (filePath + 1)); //response sent in here
        if (returnCode != RES_FORBIDDEN) {
            //pthread_mutex_lock(&logLock);
            LOG("%s,%s,%d,%d\n", method, filePath, returnCode, requestIdNum);
            fflush(logfile);
            //pthread_mutex_unlock(&logLock);
        }
        pthread_rwlock_unlock(&readLock);

        return;
    }
    //only continue if PUT and APPEND requests are made
    if (strcmp(method, "PUT") == 0 || strcmp(method, "put") == 0 || strcmp(method, "APPEND") == 0
        || strcmp(method, "append") == 0) {
        char *headerChecker = reqStr;
        while ((headerChecker = strstr(headerChecker, "\r"))) {
            if (headerChecker[1] == '\n' && headerChecker[2] == 'C') {
                break;
            }
            headerChecker += 1;
        }
        if (!headerChecker) {
            sendBadRequest(connfd, resBuff);
            (void) connfd;
            return;
            //return 400;
        }
        //REQUEST PARSING: CONTENT LENGTH, BODY
        reqParser = strstr(reqStr, "Content-Length:"); //move req parser to "Content-Length:"
        if (!reqParser) {
            sendBadRequest(connfd, resBuff);
            (void) connfd;
            return;
        }
        sscanf(reqParser, "\r\n%s %s\r\n", contLenField, contentLength);
        contentLengthInt = atoi(contentLength);

        for (unsigned long i = 0; i < strlen(contentLength); i++) {
            if (isdigit(contentLength[i]) == 0) {
                sendBadRequest(connfd, resBuff);
                (void) connfd;
                return;
            }
        }

        if (contentLengthInt < 0 || strcmp(contLenField, "Content-Length:") != 0) {
            sendBadRequest(connfd, resBuff);
            (void) connfd;
            return;
            //return 400;
        }

        //<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>
        //<><><><><><><><><><><><> -- vvv CODE TO REVIEW AND REFACTOR vvv -- <><><><><><><><><><><><>>

        //if (endOfRequestReached == NOT_GRABBED) {
        reqParser = strstr(reqParser, "\r\n");
        if (reqParser == NULL) {
            sendBadRequest(connfd, resBuff);
            (void) connfd;
            return;
        }

        reqParser = strstr(reqParser, "\r\n\r\n");
        if (reqParser == NULL) {
            sendBadRequest(connfd, resBuff);
            (void) connfd;
            return;
        }

        reqParser += 4;
        if (reqParser == NULL) {
            sendBadRequest(connfd, resBuff);
            (void) connfd;
            return;
        }
        beforeBody = reqParser - beginStr;
        bodyVal = requestLength - beforeBody;
        totalBodyLeft = contentLengthInt - bodyVal;

        char *colonMatch = strstr(beginStr, "Content-Length:");
        char *endOfColonMatch = strstr(colonMatch, "\r\n");
        int field = endOfColonMatch - colonMatch;

        for (int i = 0; i < field; i++) {

            if (colonMatch[i] == ':' && colonMatch[i + 1] != ' ') {
                if (colonMatch[i - 9] != 'l' && colonMatch[i - 7] != 'c' && colonMatch[i - 5] != 'l'
                    && colonMatch[i - 4] != 'h' && colonMatch[i - 1] != 't') {

                    strcpy(resBuff, "HTTP/1.1 400 Bad Request\r\nContent-Length: "
                                    "12\r\n\r\nBad Request\n");
                    send(connfd, resBuff, strlen(resBuff), 0);
                    (void) connfd;
                    return;
                }
            }
            if (colonMatch[i] == ' ' && colonMatch[i + 1] == ':') {
                sendBadRequest(connfd, resBuff);
                (void) connfd;
                return;
            }
        }

        if (strcmp(method, "PUT") == 0 || strcmp(method, "put") == 0) {
            //old: if(contentLengthInt > bodyVal)
            if (bodyVal > 0 || totalBodyLeft > 0
                || contentLengthInt > totalBodyLeft) { //we have more to process
                warn("bodV <%d>", bodyVal);
                char body[bodyVal];
                memset(body, 0, bodyVal);
                memcpy(body, reqParser, bodyVal);

                //returnCode = processPut(connfd, (filePath + 1), bodyVal, body);

                pthread_rwlock_wrlock(&writeLock);
                returnCode = processPutFile(connfd, (filePath + 1), totalBodyLeft, body, bodyVal);

                if (returnCode == RES_CREATED) {
                    sendCreated(connfd, resBuff);
                } else if (returnCode == RES_OK) {
                    sendOk(connfd, resBuff);
                }
                if (returnCode != RES_FORBIDDEN) {
                    //pthread_mutex_lock(&logLock);
                    LOG("%s,%s,%d,%d\n", method, filePath, returnCode, requestIdNum);
                    fflush(logfile);
                    //pthread_mutex_unlock(&logLock);
                }
                pthread_rwlock_unlock(&writeLock);
                return;
            } else if (bodyVal > contentLengthInt) { //case for when a # body bytes > cont len bytes
                char body[contentLengthInt];
                memset(body, 0, contentLengthInt);
                memcpy(body, reqParser, contentLengthInt);

                pthread_rwlock_wrlock(&writeLock);
                returnCode = processPut(connfd, (filePath + 1), contentLengthInt, body);
                if (returnCode == RES_CREATED) {
                    sendCreated(connfd, resBuff);
                } else if (returnCode == RES_OK) {
                    sendOk(connfd, resBuff);
                }
                if (returnCode != RES_FORBIDDEN) {
                    //pthread_mutex_lock(&logLock);
                    LOG("%s,%s,%d,%d\n", method, filePath, returnCode, requestIdNum);
                    fflush(logfile);
                    //pthread_mutex_unlock(&logLock);
                }
                pthread_rwlock_unlock(&writeLock);

                return;
            }

        } else if (strcmp(method, "APPEND") == 0 || strcmp(method, "append") == 0) {
            //old: if(contentLengthInt > bodyVal)
            if (bodyVal > 0 || totalBodyLeft > 0
                || contentLengthInt > totalBodyLeft) { //we have more to process
                char body[bodyVal];
                memset(body, 0, bodyVal);
                memcpy(body, reqParser, bodyVal);
                //returnCode = processAppend(connfd, (filePath + 1), bodyVal, body);
                pthread_rwlock_wrlock(&writeLock);
                returnCode
                    = processAppendFile(connfd, (filePath + 1), totalBodyLeft, body, bodyVal);
                if (returnCode == RES_OK) {
                    sendOk(connfd, resBuff);
                }
                if (returnCode != RES_FORBIDDEN) {
                    //pthread_mutex_lock(&logLock);
                    LOG("%s,%s,%d,%d\n", method, filePath, returnCode, requestIdNum);
                    fflush(logfile);
                    //pthread_mutex_unlock(&logLock);
                }
                pthread_rwlock_unlock(&writeLock);

                return;
            } else if (bodyVal > contentLengthInt) { //case for when a # body bytes > cont len bytes
                char body[contentLengthInt];
                memset(body, 0, contentLengthInt);
                memcpy(body, reqParser, contentLengthInt);

                pthread_rwlock_wrlock(&writeLock);
                returnCode = processAppend(connfd, (filePath + 1), contentLengthInt, body);
                if (returnCode == RES_OK) {
                    sendOk(connfd, resBuff);
                }
                if (returnCode != RES_FORBIDDEN) {
                    //pthread_mutex_lock(&logLock);
                    LOG("%s,%s,%d,%d\n", method, filePath, returnCode, requestIdNum);
                    fflush(logfile);
                    //pthread_mutex_unlock(&logLock);
                }
                pthread_rwlock_unlock(&writeLock);
                return;
            }
        }

        //<end of put/append method check stmt>
    }
    //<><><><><><><><><><><><> -- ^^^ CODE TO REVIEW AND REFACTOR ^^^ -- <><><><><><><><><><><><>>
    //<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

    //<end of while>
}

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

//check performance: time ./httpserver -t <threads> -l <logfile> <port>
//valgrind: valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s ./httpserver -t 4 -l logging 8081
//./get_speedup.sh 5012 README.md 64 64 1000
void *threadFunction(void *arg) {
    //long unsigned argument = (long unsigned) arg;
    int threadConnfd = 0;
    //warn("Thread: %lu", argument);
    for (;;) {
        pthread_mutex_lock(&myMutex);

        while ((threadConnfd = dequeue()) < 1) {
            //warn("queueCapCDEC: %d", QueueCapacityCount);
            printf("Thread: %p waiting...\n", arg);
            pthread_cond_wait(&canWork, &myMutex);
            //threadConnfd = dequeue(); //attempt once again to dequeue
        }
        //QueueCapacityCount--;
        pthread_cond_signal(&queueFull);

        //warn("TAW: %lu\n", argument);
        printf("Thread: %p about to unlock...", arg);
        pthread_mutex_unlock(&myMutex);
        if (threadConnfd > 0) {

            handle_connection(threadConnfd);

            pthread_mutex_lock(&closeConnMutex);
            (void) threadConnfd;
            close(threadConnfd);
            warn("Thread finished work");
            pthread_mutex_unlock(&closeConnMutex);
        }
    }
}

static void sigterm_handler(int sig) {
    if (sig == SIGTERM) {
        warnx("received SIGTERM");
        fclose(logfile);
        pthread_rwlock_destroy(&readLock);
        pthread_rwlock_destroy(&writeLock);
        exit(EXIT_SUCCESS);
    }
}

static void sigint_handler(int sig) {
    if (sig == SIGINT) {
        warnx("received SIGINT");
        fclose(logfile);
        pthread_rwlock_destroy(&readLock);
        pthread_rwlock_destroy(&writeLock);
        exit(EXIT_SUCCESS);
    }
}

static void usage(char *exec) {
    fprintf(stderr, "usage: %s [-t threads] [-l logfile] <port>\n", exec);
}

int main(int argc, char *argv[]) {
    int opt = 0;
    int threads = DEFAULT_THREAD_COUNT;
    logfile = stderr;
    pthread_rwlock_init(&readLock, NULL);
    pthread_rwlock_init(&writeLock, NULL);
    while ((opt = getopt(argc, argv, OPTIONS)) != -1) {
        switch (opt) {
        case 't':
            threads = strtol(optarg, NULL, 10);
            if (threads <= 0) {
                errx(EXIT_FAILURE, "bad number of threads");
            }
            break;
        case 'l':
            logfile = fopen(optarg, "w");
            if (!logfile) {
                errx(EXIT_FAILURE, "bad logfile");
            }
            break;
        default: usage(argv[0]); return EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        warnx("wrong number of arguments");
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    uint16_t port = strtouint16(argv[optind]);
    if (port == 0) {
        errx(EXIT_FAILURE, "bad port number: %s", argv[1]);
    }

    pthread_t threadPool[threads];
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigint_handler);

    int listenfd = create_listen_socket(port);

    for (long unsigned i = 0; i < (long unsigned) threads; i++) {
        if (pthread_create(&threadPool[i], NULL, threadFunction, (void *) (i + 1)) != 0) {
            warn("error on thread creation");
            return (EXIT_FAILURE);
        }
    }

    for (;;) {
        int connfd = accept(listenfd, NULL, NULL);
        pthread_mutex_lock(&myMutex);
        if (connfd < 0) {
            warn("accept error");
            continue;
        }

        if (getQueueSize() > MAX_QUEUE_SIZE) {
            warn("Queue Full <%d>", getQueueSize());
            pthread_cond_wait(&queueFull, &myMutex);
        }
        enqueue(connfd);
        //QueueCapacityCount++;
        warn("connfd [%d] to be input...", connfd);
        pthread_cond_signal(&canWork);
        pthread_mutex_unlock(&myMutex);
    }

    for (int i = 0; i < threads; i++) {
        pthread_join(threadPool[i], NULL);
    }

    return EXIT_SUCCESS;
}
