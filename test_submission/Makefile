CC = gcc
CFLAGS = -Wall -Wextra -g -pthread

all: producer consumer

producer: producer.c
	$(CC) $(CFLAGS) -o producer producer.c

consumer: consumer.c
	$(CC) $(CFLAGS) -o consumer consumer.c

clean:
	rm -f producer consumer *.o

.PHONY: all clean
