CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lssl -lcrypto -lpthread

SRC_DIR = src
BUILD_DIR = build

OBJS = \
    $(BUILD_DIR)/store.o \
    $(BUILD_DIR)/protocol.o \
    $(BUILD_DIR)/crypto.o \
    $(BUILD_DIR)/session.o \
    $(BUILD_DIR)/cluster.o \
    $(BUILD_DIR)/wal.o \
    $(BUILD_DIR)/raft.o \
    $(BUILD_DIR)/heartbeat.o

SERVER_BIN = $(BUILD_DIR)/ruth-server
CLIENT_BIN = $(BUILD_DIR)/ruth-client

.PHONY: all clean run-node1 run-node2 run-node3

all: $(SERVER_BIN) $(CLIENT_BIN)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(SERVER_BIN): $(BUILD_DIR)/server_main.o $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(CLIENT_BIN): $(BUILD_DIR)/cli_main.o $(BUILD_DIR)/client.o \
               $(BUILD_DIR)/protocol.o $(BUILD_DIR)/crypto.o \
               $(BUILD_DIR)/session.o
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(BUILD_DIR)/*.o $(SERVER_BIN) $(CLIENT_BIN)

# Run a 3-node cluster locally
run-node1: $(SERVER_BIN)
	$(SERVER_BIN) 1 config/cluster.conf

run-node2: $(SERVER_BIN)
	$(SERVER_BIN) 2 config/cluster.conf

run-node3: $(SERVER_BIN)
	$(SERVER_BIN) 3 config/cluster.conf