# Compiler and flags
CC = gcc
CFLAGS = -Isrc -Wall -Wextra -g
LDFLAGS =

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
LIB_DIR = lib

# Find all .c files in src directory
SOURCES = $(wildcard $(SRC_DIR)/*.c)
# Replace .c with .o and put them in obj directory
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

# Target executable name
TARGET_LIB = $(LIB_DIR)/libwebserver.a
TARGET_APP = $(BIN_DIR)/server_app

# Default target
all: $(TARGET_APP)

# Link the application
$(TARGET_APP): $(TARGET_LIB) $(OBJ_DIR)/main.o
	$(CC) $(CFLAGS) $(OBJ_DIR)/main.o -o $@ -L$(LIB_DIR) -lwebserver $(LDFLAGS)

# Create the static library
$(TARGET_LIB): $(filter-out $(OBJ_DIR)/main.o, $(OBJECTS))
	ar rcs $@ $^

# Compile source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up
clean:
	rm -rf $(OBJ_DIR) $(TARGET_LIB) $(TARGET_APP)

.PHONY: all clean 