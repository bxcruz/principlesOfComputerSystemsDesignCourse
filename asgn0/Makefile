OBJS	= split.o
SOURCE	= split.c
HEADER	= 
OUT	= split
CC	 = clang
FLAGS	 = -g -c -Wall -Werror -Wextra -pedantic 
LFLAGS	 = 

all: $(OBJS)
	$(CC) -g $(OBJS) -o $(OUT) $(LFLAGS)

split: split.o 
	$(CC) -o split split.o 

split.o: split.c
	$(CC) $(FLAGS) split.c -std=c99


clean:
	rm -f $(OBJS) $(OUT)