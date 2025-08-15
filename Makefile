# Source files
SRC = application.c RDMA_api.c server.c kv_store.c client.c

# Header files
HEADERS = RDMA_api.h server.h kv_store.h client.h

# Object files (replace .c with .o)
OBJ = $(SRC:.c=.o)

# Tar command
TAR = tar
TARFLAGS = -cvf
TARNAME = tar_file.tgz

# Compiler and flags
CC = gcc
CFLAGS = -I. -Wall -Wextra

# Default target
default: all

all: clean application

# Rule to link object files into the executable
application: $(OBJ)
	$(CC) $(OBJ) -o application -libverbs
	ln -s application client

# Rule to compile .c files into .o files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule
clean:
	rm -rf ./client ./application $(OBJ)

# Target to create a tarball of the source files
tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(SRC) $(HEADERS) Makefile

