CC=cc
CFLAGS=-Wall -g
RPC_SYSTEM=rpc.o
EXE1=server
EXE2=client

.PHONY: format all

all: $(RPC_SYSTEM) $(EXE1) $(EXE2)

$(RPC_SYSTEM): rpc.c rpc.h
	$(CC) -c -o $@ $< $(CFLAGS)

$(EXE1): server.c $(RPC_SYSTEM)
	$(CC) $(CFLAGS) -o $@ $< $(RPC_SYSTEM)

$(EXE2): client.c $(RPC_SYSTEM)
	$(CC) $(CFLAGS) -o $@ $< $(RPC_SYSTEM)


# RPC_SYSTEM_A=rpc.a
# $(RPC_SYSTEM_A): rpc.o
# 	ar rcs $(RPC_SYSTEM_A) $(RPC_SYSTEM)

# removing files
clean:
	rm -f $(RPC_SYSTEM) $(EXE1) $(EXE2)

format:
	clang-format -style=file -i *.c *.h
