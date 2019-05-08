TARGET := loadgen
KMOD := kcpuhog
CC := gcc
CFLAGS=-I. -Wall -g -O2
LDFLAGS=-lm -lrt
SRC_DIRS := .
SRCS := $(shell find $(SRC_DIRS) -maxdepth 1 -name '*.c')
OBJS := $(addsuffix .o, $(basename $(SRCS)))

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJS): $(SRCS)
	$(CC) $(CFLAGS) -c $^ -o $@

$(KMOD):
	sh -c 'cd kmod && make'

.PHONY: all clean

all: $(TARGET) $(KMOD)

clean:
	rm -rf $(OBJS) $(TARGET)
	sh -c 'cd kmod && make clean'
