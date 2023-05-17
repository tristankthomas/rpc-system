CC=cc
CFLAGS=-Wall -g
RPC_SYSTEM=rpc.o
RPC_SYSTEM_A=rpc.a
HASH_TABLE=hash_table.o
EXE1=server
EXE2=client

.PHONY: format all

all: $(RPC_SYSTEM_A) $(EXE1) $(EXE2)

$(RPC_SYSTEM): src/rpc.c src/rpc.h
	$(CC) -c -o $@ $< $(CFLAGS)

$(HASH_TABLE): src/hash_table.c src/hash_table.h
	$(CC) -c -o $@ $< $(CFLAGS)


$(RPC_SYSTEM_A): $(RPC_SYSTEM) $(HASH_TABLE)
	ar rcs $(RPC_SYSTEM_A) $(RPC_SYSTEM) $(HASH_TABLE)

$(EXE1): src/server.c $(RPC_SYSTEM_A)
	$(CC) $(CFLAGS) -o $@ $< -L. -l:rpc.a

$(EXE2): src/client.c $(RPC_SYSTEM_A)
	$(CC) $(CFLAGS) -o $@ $< -L. -l:rpc.a





# removing files
clean:
	rm -f $(RPC_SYSTEM) $(HASH_TABLE) $(RPC_SYSTEM_A) $(EXE1) $(EXE2)

format:
	clang-format -style=file -i *.c *.h
