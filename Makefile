# Compiler and flags
CC = gcc
# We need to add the include paths for our dependencies here as well
CFLAGS = -Iinclude -Wall -Wextra -g
CFLAGS += -Ideps/l8w8jwt/include
CFLAGS += -Ideps/l8w8jwt/lib/mbedtls/include
CFLAGS += -Ideps/yyjson  # Phase 3: JSON support

# Linker flags (no longer needed here for linking the final app)

# Directories
SRC_DIR = src
OBJ_DIR = obj
# BIN_DIR is no longer needed here
LIB_DIR = lib

# Find all .c files in src directory
SOURCES = $(wildcard $(SRC_DIR)/*.c)
# Replace .c with .o and put them in obj directory
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

# yyjson object file (Phase 3)
YYJSON_OBJ = $(OBJ_DIR)/yyjson.o

# All .o files in obj/ now belong to the server library
SERVER_OBJECTS = $(OBJECTS) $(YYJSON_OBJ)

# Target library names
TARGET_LIB = $(LIB_DIR)/libwebserver.a
TARGET_JWT_LIB = $(LIB_DIR)/libjwt.a
# TARGET_APP is now built by the user_backend Makefile

# Default target: build our library, which depends on the JWT library
all: $(TARGET_LIB)

# The JWT library is a dependency for our server library
$(TARGET_LIB): $(TARGET_JWT_LIB) $(SERVER_OBJECTS)
	@mkdir -p $(LIB_DIR)
	ar rcs $@ $(SERVER_OBJECTS)

# Rule to build the JWT library by calling its own Makefile
$(TARGET_JWT_LIB):
	$(MAKE) -C deps -f Makefile.jwt

# This rule is now more general for creating the library from objects
# Create our server's static library
# $(TARGET_LIB): $(SERVER_OBJECTS)
# 	@mkdir -p $(LIB_DIR)
# 	ar rcs $@ $^

# Compile our source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile yyjson (Phase 3)
$(YYJSON_OBJ): deps/yyjson/yyjson.c
	@mkdir -p $(OBJ_DIR)
	$(CC) -Ideps/yyjson -Wall -O2 -c $< -o $@

# --- Phony Targets for Build Management ---

.PHONY: all clean clean_lib jwt

# Pre-task to build the JWT library explicitly
jwt:
	$(MAKE) -C deps -f Makefile.jwt

# Clean only the library's build artifacts
clean_lib:
	rm -rf $(OBJ_DIR) $(TARGET_LIB)

# Full clean: clean our library and the JWT library
clean: clean_lib
	$(MAKE) -C deps -f Makefile.jwt clean 