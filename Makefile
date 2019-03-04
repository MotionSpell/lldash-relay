BIN?=bin

$(BIN)/evanescent.exe: \
	$(BIN)/main.cpp.o \
	$(BIN)/tcp_server.cpp.o \

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

