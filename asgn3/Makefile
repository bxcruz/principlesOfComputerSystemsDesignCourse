OBJS	= httpserver.o threadPool.o
SOURCE	= httpserver.c threadPool.c
HEADER	= threadPool.h
OUT	= httpserver
CC	 = clang
FLAGS	 = -g -c -Wall -Werror -Wextra -pedantic 
LFLAGS	 = -pthread

all: $(OBJS)
	$(CC) -g $(OBJS) -o $(OUT) $(LFLAGS)

httpserver: httpserver.o 
	$(CC) -o httpserver httpserver.o -pthread

httpserver.o: httpserver.c
	$(CC) $(FLAGS) httpserver.c -std=c99

threadPool.o: threadPool.c
	$(CC) $(FLAGS) threadPool.c -std=c99


clean:
	rm -f $(OBJS) $(OUT)
