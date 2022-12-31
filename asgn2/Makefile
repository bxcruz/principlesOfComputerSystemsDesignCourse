OBJS	= httpserver.o
SOURCE	= httpserver.c
HEADER	= 
OUT	= httpserver
CC	 = clang
FLAGS	 = -g -c -Wall -Werror -Wextra -pedantic 
LFLAGS	 = 

all: $(OBJS)
	$(CC) -g $(OBJS) -o $(OUT) $(LFLAGS)

httpserver: httpserver.o 
	$(CC) -o httpserver httpserver.o 

httpserver.o: httpserver.c
	$(CC) $(FLAGS) httpserver.c -std=c99


clean:
	rm -f $(OBJS) $(OUT)