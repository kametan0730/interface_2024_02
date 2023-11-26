#!/bin/make
OUTDIR	= ./build
TARGET	= $(OUTDIR)/curo
SOURCES	= $(wildcard *.cpp)
OBJECTS	= $(addprefix $(OUTDIR)/, $(SOURCES:.cpp=.o))

.PHONY: all
all: $(TARGET)

.PHONY: clean
clean:
	$(RM) $(OBJECTS) $(TARGET)

.PHONY: run
run: $(TARGET)
	$(TARGET)

$(TARGET): $(OBJECTS) Makefile
	$(CXX) -O0 -g -o $(TARGET) $(OBJECTS)

$(OUTDIR)/%.o: %.cpp Makefile
	mkdir -p build
	$(CXX) -O0 -g  -o $@ -c $<

.PHONY: gdb
gdb: $(TARGET)
	gdb $(TARGET) -ex "run"