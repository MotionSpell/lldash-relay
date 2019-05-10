BIN?=bin

include $(shell $(CXX) -dumpmachine | sed "s/.*-//").mk

$(BIN)/evanescent.exe: \
	$(BIN)/src/main.cpp.o \

PKGS+=openssl

CXXFLAGS+=$(shell pkg-config $(PKGS) --cflags)
LDFLAGS+=$(shell pkg-config $(PKGS) --libs)
LDFLAGS+=-pthread

#------------------------------------------------------------------------------

clean:
	rm -rf $(BIN)

$(BIN)/%.exe:
	@mkdir -p $(dir $@)
	$(CXX) -o "$@" $^ $(LDFLAGS)

$(BIN)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o "$@" $<

