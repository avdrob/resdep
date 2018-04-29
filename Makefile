TARGET := resdep_bin
CC := gcc
CFLAGS=-I. -Wall -g
LDFLAGS=-lm -lrt
SRC_DIRS := .
SRCS := $(shell find $(SRC_DIRS) -maxdepth 1 -name '*.c')
OBJS := $(addsuffix .o, $(basename $(SRCS)))

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJS): $(SRCS)
	$(CC) $(CFLAGS) -c $^ -o $@

.PHONY: all clean

all: $(TARGET)

clean:
	rm -rf $(OBJS) $(TARGET)
