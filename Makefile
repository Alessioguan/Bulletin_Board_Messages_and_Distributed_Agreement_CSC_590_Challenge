# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -pthread

# Executable name
TARGET = bbserv

# Source files
SRCS = server.c config.c fserv.c threadpool.c tcp-utils.c tokenize.c

# Object files
OBJS = $(SRCS:.c=.o)

# Header files
HEADERS = config.h fserv.h threadpool.h tcp-utils.h tokenize.h

# Default rule
all: $(TARGET)

# Rule to link the object files into the final executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Rule to compile the source files into object files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to clean up the build
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets
.PHONY: all clean
