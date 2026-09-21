CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2

INCLUDES := -I../../components/jev_core/include
SOURCES := test_command.c ../../components/jev_core/src/command.c ../../components/jev_core/src/pin_policy.c
TARGET := test_command

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
