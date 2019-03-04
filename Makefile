BIN?=bin

$(BIN)/evanescent.exe: \
	$(BIN)/src/main.cpp.o \
	$(BIN)/src/tcp_server.cpp.o \

LDFLAGS+=-pthread

#------------------------------------------------------------------------------

clean:
	rm -rf $(BIN)

$(BIN)/%.exe:
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o "$@" $^

$(BIN)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o "$@" $<

