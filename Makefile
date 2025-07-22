# Compiler and flags
CC = gcc
# We need to add the include paths for our dependencies here as well
CFLAGS = -Iinclude -Wall -Wextra -g
CFLAGS += -Ideps/l8w8jwt/include
CFLAGS += -Ideps/l8w8jwt/lib/mbedtls/include

# Linker flags - specify the library path and the libraries to link
LDFLAGS = -Llib -lwebserver -ljwt

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
LIB_DIR = lib

# Find all .c files in src directory
SOURCES = $(wildcard $(SRC_DIR)/*.c)
# Replace .c with .o and put them in obj directory
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

# We need to add our new auth.o to the library
# The main object file should be excluded from the library
SERVER_OBJECTS = $(filter-out $(OBJ_DIR)/main.o, $(OBJECTS))

# Target executable name
TARGET_LIB = $(LIB_DIR)/libwebserver.a
TARGET_JWT_LIB = $(LIB_DIR)/libjwt.a
TARGET_APP = $(BIN_DIR)/server_app

# Default target: build the app, which depends on our libraries
all: $(TARGET_APP)

# Link the application
# Depends on our main object file and both static libraries
$(TARGET_APP): $(OBJ_DIR)/main.o $(TARGET_LIB) $(TARGET_JWT_LIB)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Rule to build the JWT library by calling its own Makefile
$(TARGET_JWT_LIB):
	$(MAKE) -C deps -f Makefile.jwt

# Create our server's static library
$(TARGET_LIB): $(SERVER_OBJECTS)
	@mkdir -p $(LIB_DIR)
	ar rcs $@ $^

# Compile our source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Phony Targets for Build Management ---

.PHONY: all clean clean_app jwt

# Pre-task to build the JWT library explicitly
jwt:
	$(MAKE) -C deps -f Makefile.jwt

# Clean only the main application's build artifacts
clean_app:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(TARGET_LIB)

# Full clean: clean the app and the JWT library
clean: clean_app
	$(MAKE) -C deps -f Makefile.jwt clean 