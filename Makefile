CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lssl -lcrypto -lpthread

SRC_DIR   = src
BUILD_DIR = build

# ─── Object files ─────────────────────────────────────────────────────────────
STORE_OBJ    = $(BUILD_DIR)/store.o
PROTOCOL_OBJ = $(BUILD_DIR)/protocol.o
CLIENT_OBJ   = $(BUILD_DIR)/client.o
SERVER_OBJ   = $(BUILD_DIR)/server_main.o
CLI_OBJ      = $(BUILD_DIR)/cli_main.o

# ─── Binaries ─────────────────────────────────────────────────────────────────
SERVER_BIN = $(BUILD_DIR)/ruth-server
CLIENT_BIN = $(BUILD_DIR)/ruth-client

.PHONY: all clean run-server run-client

all: $(SERVER_BIN) $(CLIENT_BIN)

# ─── Compile each source to object ────────────────────────────────────────────
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# ─── Link server ──────────────────────────────────────────────────────────────
$(SERVER_BIN): $(SERVER_OBJ) $(STORE_OBJ) $(PROTOCOL_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

# ─── Link client ──────────────────────────────────────────────────────────────
$(CLIENT_BIN): $(CLI_OBJ) $(CLIENT_OBJ) $(PROTOCOL_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

# ─── Helpers ──────────────────────────────────────────────────────────────────
clean:
	rm -f $(BUILD_DIR)/*.o $(SERVER_BIN) $(CLIENT_BIN)

run-server: $(SERVER_BIN)
	$(SERVER_BIN)

run-client: $(CLIENT_BIN)
	$(CLIENT_BIN)

