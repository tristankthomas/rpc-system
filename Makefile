CC=cc
CFLAGS=-Wall -g
LDFLAGS=-lpthread
RPC_SYSTEM=rpc.o
RPC_SYSTEM_A=rpc.a
HASH_TABLE=hash_table.o
SERVER=rpc-server
CLIENT=rpc-client

.PHONY: format all

all: $(RPC_SYSTEM_A) $(CLIENT) $(SERVER)

$(RPC_SYSTEM): src/rpc.c src/rpc.h
	$(CC) -c -o $@ $< $(CFLAGS) $(LDFLAGS)

$(HASH_TABLE): src/hash_table.c src/hash_table.h
	$(CC) -c -o $@ $< $(CFLAGS) $(LDFLAGS)


$(RPC_SYSTEM_A): $(RPC_SYSTEM) $(HASH_TABLE)
	ar rcs $(RPC_SYSTEM_A) $(RPC_SYSTEM) $(HASH_TABLE) $(LDFLAGS)

# server and client are linked here
$(SERVER): rpc-server.c $(RPC_SYSTEM_A)
	$(CC) $(CFLAGS) -o $@ $< -L. -l:rpc.a $(LDFLAGS)

$(CLIENT): rpc-client.c $(RPC_SYSTEM_A)
	$(CC) $(CFLAGS) -o $@ $< -L. -l:rpc.a $(LDFLAGS)


# removing files
clean:
	rm -f $(RPC_SYSTEM) $(HASH_TABLE) $(RPC_SYSTEM_A) $(CLIENT) $(SERVER)

format:
	clang-format -style=file -i *.c *.h

