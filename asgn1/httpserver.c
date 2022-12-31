#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

//request-line and header-fields <= 2048 for valid reqs
#define MAXLENGTH       2048
#define GET_CONST       10
#define PUT_CONST       11
#define APPEND_CONST    12
#define RES_OK          200
#define RES_CREATED     201
#define RES_NOT_FOUND   404
#define RES_FORBIDDEN   403
#define RES_INTERNAL_SE 500

#define GRABBED     104 //10-4, all good
#define NOT_GRABBED 86 //headers are 86'd currently

/**
   Converts a string to an 16 bits unsigned integer.
   Returns 0 if the string is malformed or out of the range.
 */
uint16_t strtouint16(char number[]) {
    char *last;
    long num = strtol(number, &last, 10);
    if (num <= 0 || num > UINT16_MAX || *last != '\0') {
        return 0;
    }
    return num;
}

/**
   Creates a socket for listening for connections.
   Closes the program and prints an error message on error.
 */
int create_listen_socket(uint16_t port) {
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

    if (listen(listenfd, 500) < 0) {
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

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

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

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

int processGet(int connfd, char *filePath) {
    struct stat st;
    int getFd = open(filePath, O_RDONLY | F_OK);
    // fstat(getFd, &st);
    // int fileSize = st.st_size;
    //char *getBuffer = malloc(sizeof(char) * fileSize);
    char fileSizeStr[1024];
    char resBuff[MAXLENGTH];

    if (getFd < 0) {
        //printf("doesnt exist or no read permission");
        warn("<%d>", errno);
        if (errno == EACCES) {
            //printf("\n<EACCES triggered>\n");
            close(getFd);
            strcpy(resBuff, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n");
            send(connfd, resBuff, strlen(resBuff), 0);

            return RES_FORBIDDEN;
        }
        if (errno == ENOENT) {
            //printf("\n<ENOENT triggered>\n");
            close(getFd);
            strcpy(resBuff, "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n");
            send(connfd, resBuff, strlen(resBuff), 0);

            return RES_NOT_FOUND;
        } else {
            warn("why");
            close(getFd);
            strcpy(resBuff, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: "
                            "22\r\n\r\nInternal Server Error\n");
            send(connfd, resBuff, strlen(resBuff), 0);
            return RES_INTERNAL_SE;
        }

    } else {
        fstat(getFd, &st);
        int fileSize = st.st_size;
        //char *getBuffer = malloc(sizeof(char) * fileSize);
        char chunk[MAXLENGTH];
        int bytesRead;
        int totalGotten = 0;

        //printf("\nread returns: %zd\n", read(getFd, getBuffer, fileSize));
        strcpy(resBuff, "HTTP/1.1 200 OK\r\nContent-Length: ");
        sprintf(fileSizeStr, "%d", fileSize);
        strcat(resBuff, fileSizeStr);
        strcat(resBuff, "\r\n\r\n");
        send(connfd, resBuff, strlen(resBuff), 0);

        while (totalGotten < fileSize) {
            bytesRead = read(getFd, chunk, MAXLENGTH);
            if (bytesRead < MAXLENGTH) {
                totalGotten += bytesRead;
                send(connfd, chunk, bytesRead, 0);
                break;
            }
            send(connfd, chunk, MAXLENGTH, 0);
            totalGotten += MAXLENGTH;
        }
        close(getFd);
        //send(connfd, resBuff, strlen(resBuff), 0);
        //printf("\nwrite returns: %zd\n", send(connfd, getBuffer, fileSize, 0));
        //free(getBuffer);
        return RES_OK;
    }
    close(getFd);
    return RES_INTERNAL_SE;
}

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

int processPut(int connfd, char *filePath, int contLen, char *body) {
    //printf("%d %s", bytesRead, putBody);

    int putFd;
    int created = 0;

    if (access(filePath, F_OK) != -1) {
        created = 0;
    } else {
        created = 1;
    }

    //consolidate the open call and add reason code
    putFd = open(
        filePath, O_RDWR | O_CREAT | O_TRUNC, 0644); // one call instead of 2 alternate calls?

    //char putBuffer[contLen];
    /*

    strncpy(putBuffer, putBody, contLen);
    */

    if (putFd < 0) {
        char resBuff[MAXLENGTH];
        if (errno == EACCES) {

            close(putFd);
            strcpy(resBuff, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n");
            send(connfd, resBuff, strlen(resBuff), 0);
            return RES_FORBIDDEN;
        }
        if (errno != 0) {
            created = -1;
            close(putFd);
            warn("hihi");
            strcpy(resBuff, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: "
                            "22\r\n\r\nInternal Server Error\n");
            send(connfd, resBuff, strlen(resBuff), 0);
            return RES_INTERNAL_SE;
        }
    } else {

        write(putFd, body, contLen);

        close(putFd);
        char resBuff[MAXLENGTH];
        if (created == 1) {
            strcpy(resBuff, "HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\nCreated\n");
            send(connfd, resBuff, strlen(resBuff), 0);
            return RES_CREATED;
        }
        if (created == 0) {
            strcpy(resBuff, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n");
            send(connfd, resBuff, strlen(resBuff), 0);
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
    putFd = open(
        filePath, O_RDWR | O_CREAT | O_TRUNC, 0644); // one call instead of 2 alternate calls?

    if (putFd < 0) {
        char resBuff[MAXLENGTH];
        //stop processing and exit on any error
        if (errno == EACCES) {

            close(putFd);
            strcpy(resBuff, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n");
            send(connfd, resBuff, strlen(resBuff), 0);
            return RES_FORBIDDEN;
        }
        if (errno != 0) {
            created = -1;
            close(putFd);
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
        char chunk[MAXLENGTH];
        while (totalToReceive < contLen) {
            bytesRead = recv(connfd, chunk, MAXLENGTH, 0);
            if (bytesRead == 0) {
                warn("hi there are no more bytes to read");
                break;
            }
            warn("hi i continue reading");
            if (bytesRead < MAXLENGTH && (bytesRead + totalToReceive) == contLen) {
                warn("bytRR<%d>", bytesRead);
                warn("MX - bytRR<%d>", MAXLENGTH - bytesRead);
                totalToReceive += bytesRead;
                warn("%d", totalToReceive);
                write(putFd, chunk, bytesRead);

                break;
            }
            write(putFd, chunk, bytesRead);
            totalToReceive += bytesRead;
        }
        warn("no more bytes to read");
        close(putFd);
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
    char resBuff[MAXLENGTH];
    int appendFd = open(filePath, O_RDWR | O_APPEND, 0644);
    if (appendFd < 0) {
        if (errno == EACCES) {
            //printf("\n<EACCES triggered>\n");

            close(appendFd);
            strcpy(resBuff, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n");
            send(connfd, resBuff, strlen(resBuff), 0);
            return RES_FORBIDDEN;
        }
        if (errno == ENOENT) {
            //printf("\n<something triggered>\n");

            close(appendFd);
            strcpy(resBuff, "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n");
            send(connfd, resBuff, strlen(resBuff), 0);
            return RES_NOT_FOUND;

        } else {
            close(appendFd);
            strcpy(resBuff, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: "
                            "22\r\n\r\nInternal Server Error\n");
            send(connfd, resBuff, strlen(resBuff), 0);
            return RES_INTERNAL_SE;
        }
    }

    int location = lseek(appendFd, 0, SEEK_END);
    printf("\nlocation <%d>", location);
    //write(appendFd, appendBody, contLen);
    write(appendFd, body, contLen);

    // if ((write(appendFd, body, contLen)) == -1) {
    //     strcpy(resBuff, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: "
    //                     "22\r\n\r\nInternal Server Error\n");
    //     send(connfd, resBuff, strlen(resBuff), 0);
    //     close(appendFd);
    //     return RES_INTERNAL_SE;
    // }

    close(appendFd);
    strcpy(resBuff, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n");
    send(connfd, resBuff, strlen(resBuff), 0);
    return RES_OK;
}

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

int processAppendFile(
    int connfd, char *filePath, int contLen, char *firstBodyChunk, int firstBodyChunkVal) {
    char resBuff[MAXLENGTH];
    int appendFd = open(filePath, O_RDWR | O_APPEND, 0644);
    if (appendFd < 0) {
        //send error immediately. do not continue processing
        if (errno == EACCES) {
            //printf("\n<EACCES triggered>\n");

            close(appendFd);
            strcpy(resBuff, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n");
            send(connfd, resBuff, strlen(resBuff), 0);
            return RES_FORBIDDEN;
        }
        if (errno == ENOENT) {
            sendNotFound(connfd, resBuff);
            close(appendFd);
            return RES_NOT_FOUND;

        } else {
            sendInternalServerError(connfd, resBuff);
            close(appendFd);
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
    char chunk[MAXLENGTH];
    while (totalToReceive < contLen) {
        bytesRead = recv(connfd, chunk, MAXLENGTH, 0);
        if (bytesRead == 0) {
            break;
        }
        if (bytesRead < MAXLENGTH && (bytesRead + totalToReceive) == contLen) {
            warn("bytRR<%d>", bytesRead);
            warn("MX - bytRR<%d>", MAXLENGTH - bytesRead);
            totalToReceive += bytesRead;
            warn("%d", totalToReceive);
            write(appendFd, chunk, bytesRead);

            break;
        }
        write(appendFd, chunk, bytesRead);
        totalToReceive += bytesRead;
    }

    close(appendFd);
    //send OK outside once finished APPENDing
    return RES_OK;
}

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

//==================================================================================================
//**************************************************************************************************
//==================================================================================================

void handle_connection(int connfd) {
    // make the compiler not complain
    char *reqParser; //char ptr to only get correct fields for reqs (ignore others)
    char reqString[MAXLENGTH];
    char method[2048]; //method string
    char filePath[2048]; //filePath string
    char version[2048]; //version string
    char contentLength[2048]; //content-length string
    char contLenField[2048]; //field that contains str "Content-Length"
    char resBuff[MAXLENGTH];

    int bytesRead;
    int returnCode;
    int requestLength;
    int contentLengthInt;
    int beforeBody;
    int bodyVal;
    int totalBodyLeft;

    int headersGrabbedMinusContLen = NOT_GRABBED;
    int contentLengthGrabbed = NOT_GRABBED;
    int endOfRequestReached = NOT_GRABBED; //make it to "\r\n\r\n"?

    //bytesRead = recv(connfd, reqStr, MAXLENGTH,0);
    //warn("<%d>", bytesRead);
    while ((bytesRead = recv(connfd, reqString, MAXLENGTH, 0)) > 0) {
        requestLength = bytesRead;
        //printf(">>%d<<", mtfFlag);
        //warn("<bytes Read %d>", bytesRead);
        char *reqStr = reqString;
        char *beginStr = reqStr;

        char *keyValCheck = reqStr;
        //warn("<%s>", reqStr);
        //keyValCheck = strstr(keyValCheck, "\r\n");
        int parse = 0;

        while ((keyValCheck = strstr(keyValCheck, "\r\n"))) {
            //printf("\n<%s>\n", keyValCheck);
            while (checkForASCII(keyValCheck[parse]) == 1 && keyValCheck[parse] != '\r') {
                //printf("<%c>", keyValCheck[parse]);
                parse++;
            }
            if (checkForASCII(keyValCheck[parse]) == 0) {
                warn("noooo");
                strcpy(
                    resBuff, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                send(connfd, resBuff, strlen(resBuff), 0);
                (void) connfd;
                return;
                //return 400;
            }
            parse = 0;
            keyValCheck += 1;
        }

        if (headersGrabbedMinusContLen == NOT_GRABBED) {
            //REQUEST PARSING: METHOD, FILEPATH, VERSION (ONLY FIELDS NEEDED FOR GET)
            sscanf(reqStr, "%s[a-zA-Z]{1,8}", method); //grab method
            reqStr = strstr(reqStr, " /"); //next sscanf() acting up
            if (reqStr == NULL) {
                warn("noooo1");
                strcpy(
                    resBuff, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                send(connfd, resBuff, strlen(resBuff), 0);
                (void) connfd;
                return;
                //return 400;
            }
            sscanf(reqStr, "%s/[a-zA-Z0-9._]{1,19} ", filePath); //grab filepath
            reqStr = strstr(reqStr, " HTTP"); //sscanf() acting up
            if (!reqStr) {
                warn("noooo2");
                strcpy(
                    resBuff, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                send(connfd, resBuff, strlen(resBuff), 0);
                (void) connfd;
                return;
                //return 400;
            }
            for (unsigned long i = 0; i < strlen(filePath); i++) {
                if (checkForASCII(filePath[i]) == 0) {

                    warn("noooo3 <%c>", filePath[i]);
                    strcpy(resBuff,
                        "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                    send(connfd, resBuff, strlen(resBuff), 0);
                    (void) connfd;
                    return;
                }
            }
            sscanf(reqStr, "%sHTTP/[0-9].[0-9]\r\n", version); //grab HTTP version

            //printf("\nMethod: %s\nFile Path: %s\nVersion: %s\n", method, filePath + 1, version);

            if (strlen(method) > 8 || strlen(method) < 3 || filePath[0] != '/'
                || strlen(filePath) > 19 || strlen(filePath + 1) < 1
                || strcmp(version, "HTTP/1.1") != 0) {
                warn("filePathWithSlash: <%s>, filePathW/OSlash: <%s>", filePath, filePath + 1);
                strcpy(
                    resBuff, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                send(connfd, resBuff, strlen(resBuff), 0);
                (void) connfd;
                return;
                //return 400;
            }
            if (strcmp(method, "GET") != 0 && strcmp(method, "get") != 0
                && strcmp(method, "PUT") != 0 && strcmp(method, "put") != 0
                && strcmp(method, "APPEND") != 0 && strcmp(method, "append") != 0) {
                // printf("TODO: write response for 501 Not Implemented Content-Length: 15\r\n\r\nNot "
                //        "Implemented");
                strcpy(resBuff,
                    "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n");
                send(connfd, resBuff, strlen(resBuff), 0);
                (void) connfd;
                return;
                //return 501;
            }
            headersGrabbedMinusContLen = GRABBED;
        }

        if (strcmp(method, "GET") == 0 || strcmp(method, "get") == 0) {
            returnCode = processGet(connfd, (filePath + 1));
            (void) connfd;
            return;
            //return returnCode;
        }
        //only continue if PUT and APPEND requests are made
        if (strcmp(method, "PUT") == 0 || strcmp(method, "put") == 0
            || strcmp(method, "APPEND") == 0 || strcmp(method, "append") == 0) {

            if (contentLengthGrabbed == NOT_GRABBED) {

                char *headerChecker = reqStr;
                while ((headerChecker = strstr(headerChecker, "\r"))) {
                    //printf("%s", headerChecker);
                    if (headerChecker[1] == '\n' && headerChecker[2] == 'C') {
                        //printf("\n<we got out>\n");
                        break;
                    }
                    headerChecker += 1;
                }
                if (!headerChecker) {
                    //printf("<%s>", headerChecker);
                    strcpy(resBuff,
                        "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                    send(connfd, resBuff, strlen(resBuff), 0);
                    (void) connfd;
                    return;
                    //return 400;
                }

                reqParser
                    = strstr(reqStr, "Content-Length:"); //move req parser to "Content-Length:"
                if (!reqParser) {
                    strcpy(resBuff,
                        "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                    send(connfd, resBuff, strlen(resBuff), 0);
                    (void) connfd;
                    return;
                    //return 400;
                }
                sscanf(reqParser, "\r\n%s %s\r\n", contLenField,
                    contentLength); //grab contentLength size (int)
                //printf("\nctf<%s> cl<%s>\n", contLenField, contentLength);
                for (unsigned long i = 0; i < strlen(contentLength); i++) {
                    warn("<%c>", contentLength[i]);
                    if (isdigit(contentLength[i]) == 0) {
                        strcpy(resBuff,
                            "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                        send(connfd, resBuff, strlen(resBuff), 0);
                        (void) connfd;
                        return;
                    }
                }
                contentLengthInt = atoi(contentLength);
                if (contentLengthInt < 0 || strcmp(contLenField, "Content-Length:") != 0) {
                    strcpy(resBuff,
                        "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                    send(connfd, resBuff, strlen(resBuff), 0);
                    (void) connfd;
                    return;
                    //return 400;
                }
                contentLengthGrabbed = GRABBED;
            }
            //*****************************************************************************************
            //printf("\n<we got out1>\n");
            //printf("\n<we got out2>\n");
            //****************************************************************************************

            //REQUEST PARSING: CONTENT LENGTH, BODY
            //reqParser = strstr(reqStr, "\r\nContent-Length:"); //move req parser to "Content-Length:"
            //printf("<<<<content length %d>>>>\n", contentLengthInt);

            ////////////////////////////////////////////////////////////////////////////////////
            if (endOfRequestReached == NOT_GRABBED) {
                reqParser = strstr(reqParser, "\r\n");
                if (reqParser == NULL) {
                    strcpy(resBuff,
                        "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                    send(connfd, resBuff, strlen(resBuff), 0);
                    (void) connfd;
                    return;
                }
                reqParser = strstr(reqParser, "\r\n\r\n");
                if (reqParser == NULL) {
                    strcpy(resBuff,
                        "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                    send(connfd, resBuff, strlen(resBuff), 0);
                    (void) connfd;
                    return;
                }
                warn("<%s>", reqParser);
                reqParser += 4;
                if (reqParser == NULL) {
                    strcpy(resBuff,
                        "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                    send(connfd, resBuff, strlen(resBuff), 0);
                    (void) connfd;
                    return;
                }
                beforeBody = reqParser - beginStr;
                bodyVal = requestLength - beforeBody;
                totalBodyLeft = contentLengthInt - bodyVal;
                warn("b4Body<%d> reqLen<%d> bodyVal<%d> reqp<%s> totalBodL <%d>", beforeBody,
                    requestLength, bodyVal, reqParser, totalBodyLeft);

                char *colonMatch = strstr(beginStr, "Content-Length:");
                char *endOfColonMatch = strstr(colonMatch, "\r\n");
                int field = endOfColonMatch - colonMatch;

                for (int i = 0; i < field; i++) {
                    warn("cM <%c>", colonMatch[i]);

                    if (colonMatch[i] == ':' && colonMatch[i + 1] != ' ') {
                        if (colonMatch[i - 9] != 'l' && colonMatch[i - 7] != 'c'
                            && colonMatch[i - 5] != 'l' && colonMatch[i - 4] != 'h'
                            && colonMatch[i - 1] != 't') {

                            warn("fuck!");
                            strcpy(resBuff, "HTTP/1.1 400 Bad Request\r\nContent-Length: "
                                            "12\r\n\r\nBad Request\n");
                            send(connfd, resBuff, strlen(resBuff), 0);
                            (void) connfd;
                            return;
                        }
                    }
                    if (colonMatch[i] == ' ' && colonMatch[i + 1] == ':') {
                        warn("fuck!1");
                        strcpy(resBuff,
                            "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
                        send(connfd, resBuff, strlen(resBuff), 0);
                        (void) connfd;
                        return;
                    }
                }
                endOfRequestReached = GRABBED;
            }

            //char recvBody[contentLengthInt];

            if (strcmp(method, "PUT") == 0 || strcmp(method, "put") == 0) {
                //old: if(contentLengthInt > bodyVal)
                if (totalBodyLeft > 0
                    || contentLengthInt > totalBodyLeft) { //we have more to process

                    char body[bodyVal];
                    memcpy(body, reqParser, bodyVal);
                    warn("<%s>", body);
                    //returnCode = processPut(connfd, (filePath + 1), bodyVal, body);
                    warn("ive entered process put File");
                    returnCode
                        = processPutFile(connfd, (filePath + 1), totalBodyLeft, body, bodyVal);

                    if (returnCode == RES_CREATED) {
                        sendCreated(connfd, resBuff);
                    } else if (RES_OK) {
                        sendOk(connfd, resBuff);
                    }
                    warn("seg1");
                    (void) connfd;
                    return;
                } else if (bodyVal
                           > contentLengthInt) { //case for when a # body bytes > cont len bytes
                    char body[contentLengthInt];
                    memcpy(body, reqParser, contentLengthInt);
                    warn("ive entered process put");
                    returnCode = processPut(connfd, (filePath + 1), contentLengthInt, body);
                    if (returnCode == RES_CREATED) {
                        sendCreated(connfd, resBuff);
                    } else if (returnCode == RES_OK) {
                        sendOk(connfd, resBuff);
                    }
                    warn("seg2");
                    (void) connfd;
                    return;
                }
                warn("bye bye no process put");
            } else if (strcmp(method, "APPEND") == 0 || strcmp(method, "append") == 0) {
                //old: if(contentLengthInt > bodyVal)
                if (totalBodyLeft > 0
                    || contentLengthInt > totalBodyLeft) { //we have more to process
                    char body[bodyVal];
                    memcpy(body, reqParser, bodyVal);
                    //returnCode = processAppend(connfd, (filePath + 1), bodyVal, body);
                    returnCode
                        = processAppendFile(connfd, (filePath + 1), totalBodyLeft, body, bodyVal);
                    if (returnCode == RES_OK) {
                        sendOk(connfd, resBuff);
                    }
                    (void) connfd;
                    return;
                } else if (bodyVal
                           > contentLengthInt) { //case for when a # body bytes > cont len bytes
                    char body[contentLengthInt];
                    memcpy(body, reqParser, contentLengthInt);
                    returnCode = processAppend(connfd, (filePath + 1), contentLengthInt, body);
                    if (returnCode == RES_OK) {
                        sendOk(connfd, resBuff);
                    }

                    (void) connfd;
                    return;
                }
            }
        }

        //return RES_INTERNAL_SE;
    }
    if (bytesRead < 0) {
        warn("recv error");
        sendInternalServerError(connfd, resBuff);
        (void) connfd;
        return;
        //exit(-1);
    }
    (void) connfd;
}

int main(int argc, char *argv[]) {
    int listenfd;
    uint16_t port;

    if (argc != 2) {
        errx(EXIT_FAILURE, "wrong arguments: %s port_num", argv[0]);
    }

    port = strtouint16(argv[1]);
    if (port == 0) {
        errx(EXIT_FAILURE, "invalid port number: %s", argv[1]);
    }
    listenfd = create_listen_socket(port);

    signal(SIGPIPE, SIG_IGN);

    while (1) {
        int connfd = accept(listenfd, NULL, NULL);
        if (connfd < 0) {
            warn("accept error");
            continue;
        }
        handle_connection(connfd);

        // good code opens and closes objects in the same context. *sigh*
        close(connfd);
    }

    return EXIT_SUCCESS;
}
