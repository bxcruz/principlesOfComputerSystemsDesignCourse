#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>

//NOTES:
// ./split <DELIM> <FILES or -> ... <FILES OR ->
// syscalls 0/1/2 -> in/out/error

#define BUFFSIZE 1024

int checkForASCII(char c) {
    //if alphanumeric or stuff like ! ' " # $
    if (isprint(c) != 0 || iscntrl(c) != 0 || isblank(c) != 0 || isspace(c) != 0 || c == '\0') {
        return 1;
    }
    return 0;
}

//modularize stdin flow
void processStdin(int stdinFlag, char delim) {
    int errNum;
    char stdinBuffer[BUFFSIZE];
    int bytesRead;
    while ((bytesRead = read(stdinFlag, stdinBuffer, BUFFSIZE))
           > 0) { //if read returns 0, then it is at EOF
        for (int i = 0; i < bytesRead; i++) {
            if (stdinBuffer[i] == delim) {
                stdinBuffer[i] = '\n';
            }
        }
        if ((write(STDOUT_FILENO, stdinBuffer, bytesRead)) == -1) {
            errNum = errno;
            fprintf(stderr, "Write error %d", errno);
            return;
        };
    }
    if (bytesRead == -1) {
        errNum = errno;
        fprintf(stderr, "Read error %d", errno);
        return;
    }
}

//modularize file flow
void processFile(int fileD, char delim) {
    //create string buffer for chunks of text/bin file
    char inputBuffer[BUFFSIZE];
    int bytesRead;
    int errNum = 0;
    while ((bytesRead = read(fileD, inputBuffer, BUFFSIZE)) > 0) {
        for (int i = 0; i < bytesRead; i++) {
            if (inputBuffer[i] == delim) {
                inputBuffer[i] = '\n';
            }
        }
        if ((write(STDOUT_FILENO, inputBuffer, bytesRead)) == -1) {
            //280 error most likely happens here
            errNum = errno;
            fprintf(stderr, "Write error (%d)", errno);

            return;
        }
    }
    if (bytesRead == -1) {
        errNum = errno;
        fprintf(stderr, "Read error (%d)", errno);
        return;
    }
    //printf("test\n");
}

int main(int argc, char *argv[]) {
    int fileDescriptor;
    char delimiter;
    int errNum;

    //command format check
    if (argc <= 2) {
        errNum = errno;
        fprintf(stderr,
            "Not enough arguments usage: ./split: <split_char> [<file1> <file2> ...] %d", errno);
        exit(22); //error value based on resources binary
    }

    //character length 1 check
    if (strlen(argv[1]) > 1) {
        errNum = errno;
        fprintf(stderr, "Cannot handle multi-character splits: %s %d", argv[1], errno);
        //warn("fuck\n");
        return (22);
    }

    delimiter = *argv[1];

    //go through each command line argument (text file or stdin stream)
    //start at 3rd argument, which is the first text file or the stdin character '-'
    for (int totalArgs = 2; totalArgs < argc; totalArgs++) {
        if (strncmp(argv[totalArgs], "-", 1) == 0) {
            processStdin(STDIN_FILENO, delimiter);
        } else if ((fileDescriptor = open(argv[totalArgs], O_RDONLY)) == -1) {
            errNum = errno;
            fprintf(stderr, "split: <%s>: No such file or directory %d", argv[totalArgs], errno);
            //exit(2);
            continue; //Do not die outright
        } else {
            processFile(fileDescriptor, delimiter);
            if (close(fileDescriptor) == -1) {
                errNum = errno;
                fprintf(stderr, "Close file error %d", errno);
                continue;
            }
        }
    }

    if (errno != 0) {
        exit(errno);
    }
    return 0;
}
