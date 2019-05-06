$(BIN)/evanescent.exe: \
	$(BIN)/src/tcp_server_mingw32.cpp.o

LDFLAGS+=-lws2_32
