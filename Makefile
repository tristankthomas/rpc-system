CC=cc
CFLAGS=-Wall -g
LDFLAGS=-lpthread
RPC_SYSTEM=rpc.o
RPC_SYSTEM_A=rpc.a
HASH_TABLE=hash_table.o

.PHONY: format all

all: $(RPC_SYSTEM_A)

$(RPC_SYSTEM): src/rpc.c src/rpc.h
	$(CC) -c -o $@ $< $(CFLAGS) $(LDFLAGS)

$(HASH_TABLE): src/hash_table.c src/hash_table.h
	$(CC) -c -o $@ $< $(CFLAGS) $(LDFLAGS)


# removing files
clean:
	rm -f $(RPC_SYSTEM) $(HASH_TABLE) $(RPC_SYSTEM_A)

format:
	clang-format -style=file -i *.c *.h
