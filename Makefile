TARGET := resdep
CC := gcc
SRC_DIRS := .
SRCS := $(shell find $(SRC_DIRS) -maxdepth 1 -name '*.c')
OBJS := $(addsuffix .o, $(basename $(SRCS)))

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@

$(OBJS): $(SRCS)
	$(CC) -c $^ -o $@

.PHONY: all clean

all: $(TARGET)

clean:
	rm -rf $(OBJS) $(TARGET)
