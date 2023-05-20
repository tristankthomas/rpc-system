CC=cc
CFLAGS=-Wall -g
LDFLAGS=-lpthread
RPC_SYSTEM=rpc.o
RPC_SYSTEM_A=rpc.a
HASH_TABLE=hash_table.o
EXE1=server
EXE2=client
EXE3=server_test
EXE4=client_test

.PHONY: format all

all: $(RPC_SYSTEM_A) $(EXE1) $(EXE2) $(EXE3) $(EXE4)

$(RPC_SYSTEM): src/rpc.c src/rpc.h
	$(CC) -c -o $@ $< $(CFLAGS) $(LDFLAGS)

$(HASH_TABLE): src/hash_table.c src/hash_table.h
	$(CC) -c -o $@ $< $(CFLAGS) $(LDFLAGS)


$(RPC_SYSTEM_A): $(RPC_SYSTEM) $(HASH_TABLE)
	ar rcs $(RPC_SYSTEM_A) $(RPC_SYSTEM) $(HASH_TABLE) $(LDFLAGS)

$(EXE1): src/server.c $(RPC_SYSTEM_A)
	$(CC) $(CFLAGS) -o $@ $< -L. -l:rpc.a $(LDFLAGS)

$(EXE2): src/client.c $(RPC_SYSTEM_A)
	$(CC) $(CFLAGS) -o $@ $< -L. -l:rpc.a $(LDFLAGS)



$(EXE3): artifacts/server.a $(RPC_SYSTEM_A)
	$(CC) $(CFLAGS) -o $@ $< $(RPC_SYSTEM_A) $(LDFLAGS)

$(EXE4): artifacts/client.a $(RPC_SYSTEM_A)
	$(CC) $(CFLAGS) -o $@ $< $(RPC_SYSTEM_A) $(LDFLAGS)

# removing files
clean:
	rm -f $(RPC_SYSTEM) $(HASH_TABLE) $(RPC_SYSTEM_A) $(EXE1) $(EXE2)

format:
	clang-format -style=file -i *.c *.h
