# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -g
LIBS = -lcurl -lpthread

# Target executable
TARGET = r.out

# Source files
SOURCES = main.c functions.c

# Object files
OBJECTS = $(SOURCES:.c=.o)

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LIBS)

# Compile source files to object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean compiled files
clean:
	rm -f *.o *.out

# Clear screen
clear:
	clear

# Run the program
run: clear clean $(TARGET)
	./$(TARGET) url.txt 4 100

# Phony targets (not actual files)
.PHONY: all clean clear run