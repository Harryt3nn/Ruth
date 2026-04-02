# ─── Compiler & Flags ─────────────────────────────────────────────────────────
CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lssl -lcrypto -lpthread

# ─── Directories ──────────────────────────────────────────────────────────────
SRC_DIR   = src
BUILD_DIR = build
INC_DIR   = include

# ─── Source Files ─────────────────────────────────────────────────────────────
STORE_SRC  = $(SRC_DIR)/store.c
SERVER_SRC = $(SRC_DIR)/server_main.c
CLIENT_SRC = $(SRC_DIR)/cli_main.c

# ─── Object Files ─────────────────────────────────────────────────────────────
STORE_OBJ  = $(BUILD_DIR)/store.o
SERVER_OBJ = $(BUILD_DIR)/server_main.o
CLIENT_OBJ = $(BUILD_DIR)/cli_main.o

# ─── Targets ──────────────────────────────────────────────────────────────────
SERVER_BIN = $(BUILD_DIR)/ruth-server
CLIENT_BIN = $(BUILD_DIR)/ruth-client

# ─── Default target ───────────────────────────────────────────────────────────
.PHONY: all clean

all: $(SERVER_BIN) $(CLIENT_BIN)

# ─── Build store object (shared by both binaries) ─────────────────────────────
$(STORE_OBJ): $(STORE_SRC) $(INC_DIR)/store.h $(INC_DIR)/khash.h
	$(CC) $(CFLAGS) -c $< -o $@

# ─── Server binary ────────────────────────────────────────────────────────────
$(SERVER_OBJ): $(SERVER_SRC) $(INC_DIR)/store.h $(INC_DIR)/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SERVER_BIN): $(SERVER_OBJ) $(STORE_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

# ─── Client binary ────────────────────────────────────────────────────────────
$(CLIENT_OBJ): $(CLIENT_SRC) $(INC_DIR)/client.h $(INC_DIR)/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

$(CLIENT_BIN): $(CLIENT_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

# ─── Helpers ──────────────────────────────────────────────────────────────────
clean:
	rm -f $(BUILD_DIR)/*.o $(SERVER_BIN) $(CLIENT_BIN)

run-server: $(SERVER_BIN)
	$(SERVER_BIN)

run-client: $(CLIENT_BIN)
	$(CLIENT_BIN)