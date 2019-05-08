$(BIN)/evanescent.exe: \
	$(BIN)/src/tcp_server_windows.cpp.o

LDFLAGS+=-lws2_32
