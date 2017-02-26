CC = g++
DEFINES = -DEF_DEBUG=1
CFLAGS = -W -Wall -g $(DEFINES) -Wno-write-strings
LDFLAGS = 

CODE_DIR=code/
BUILD_DIR=build/
SRC = $(wildcard $(CODE_DIR)*.cpp)
OBJS = $(patsubst $(CODE_DIR)%.cpp,$(BUILD_DIR)%.o,$(SRC))
AOUT = go-muscu

all: $(AOUT)

$(AOUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)%.o: $(CODE_DIR).cpp $(CODE_DIR)%.h $(CODE_DIR)ef_utils.h $(CODE_DIR)common.h
	$(CC) $(CFLAGS) -o $@ -c $<

$(BUILD_DIR)%.o: $(CODE_DIR)%.cpp $(CODE_DIR)ef_utils.h $(CODE_DIR)common.h
	$(CC) $(CFLAGS) -o $@ -c $<

$(BUILD_DIR)main.o: $(CODE_DIR)parsing.h

clean:
	@rm $(BUILD_DIR)*

run:
	./$(AOUT)

runv:
	valgrind ./$(AOUT)

install:
	@mkdir -p "${HOME}/.config/go-muscu/programs"
	@ln -sf "$(realpath ${AOUT})" /usr/bin/go-muscu

uninstall:
	@rm -f /usr/bin/go-muscu

purge: uninstall
	@rm -rf "${HOME}/.config/go-muscu"

.PHONY: clean run runv install uninstall purge
