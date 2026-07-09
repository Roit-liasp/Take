CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
INCLUDES := -Iinclude
PREFIX   ?= /usr/local

.PHONY: all install uninstall clean

all: take

take: src/main.cpp include/toml.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) src/main.cpp -o take

install: take
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 take $(DESTDIR)$(PREFIX)/bin/take

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/take

clean:
	rm -f take
