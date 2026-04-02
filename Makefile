CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lssl -lcrypto -lpthread

SRC_DIR   = src
BUILD_DIR = build

STORE_OBJ    = $(BUILD_DIR)/store.o
PROTOCOL_OBJ = $(BUILD_DIR)/protocol.o
CRYPTO_OBJ   = $(BUILD_DIR)/crypto.o
SESSION_OBJ  = $(BUILD_DIR)/session.o
CLIENT_OBJ   = $(BUILD_DIR)/client.o
SERVER_OBJ   = $(BUILD_DIR)/server_main.o
CLI_OBJ      = $(BUILD_DIR)/cli_main.o

SERVER_BIN = $(BUILD_DIR)/ruth-server
CLIENT_BIN = $(BUILD_DIR)/ruth-client

.PHONY: all clean run-server run-client

all: $(SERVER_BIN) $(CLIENT_BIN)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(SERVER_BIN): $(SERVER_OBJ) $(STORE_OBJ) $(PROTOCOL_OBJ) $(CRYPTO_OBJ) $(SESSION_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

$(CLIENT_BIN): $(CLI_OBJ) $(CLIENT_OBJ) $(PROTOCOL_OBJ) $(CRYPTO_OBJ) $(SESSION_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(BUILD_DIR)/*.o $(SERVER_BIN) $(CLIENT_BIN)

run-server: $(SERVER_BIN)
	$(SERVER_BIN)

run-client: $(CLIENT_BIN)
	$(CLIENT_BIN)