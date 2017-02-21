CC = g++
DEFINES = -DEF_DEBUG=1
CFLAGS = -W -Wall -g $(DEFINES)
LDFLAGS = 

BUILD_DIR=../build/
AOUT_DIR=../
SRC = $(wildcard *.cpp)
OBJS = $(patsubst %.cpp,$(BUILD_DIR)%.o,$(SRC))
AOUT = $(AOUT_DIR)main

all: $(AOUT)

$(AOUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)%.o: %.cpp %.h ef_utils.h
	$(CC) $(CFLAGS) -o $@ -c $<

$(BUILD_DIR)%.o: %.cpp ef_utils.h
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	@rm $(BUILD_DIR)*

run:
	./$(AOUT)

runv:
	valgrind ./$(AOUT)
