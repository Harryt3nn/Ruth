CC = gcc
FLAGS = -Wall -Wextra

all: server client

server: Server.c
	$(CC) $(FLAGS) -o server Server.c

client: Client.c
	$(CC) $(FLAGS) -o client Client.c

clean:
	rm -f server client