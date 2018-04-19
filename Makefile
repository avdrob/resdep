TARGET := resdep
CC := gcc
CFLAGS=-Wall -g
LDFLAGS=-lm -pthread -lrt
SRC_DIRS := .
SRCS := $(shell find $(SRC_DIRS) -name *.c)
OBJS := $(addsuffix .o, $(basename $(SRCS)))

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJS): $(SRCS)
	$(CC) $(CFLAGS) -c $^ -o $@

.PHONY: all clean

all: $(TARGET)

clean:
	rm -rf $(OBJS) $(TARGET)
