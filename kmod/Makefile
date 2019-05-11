EXTRA_CFLAGS := -I$(src)/.. -O2
obj-m += kloadgend.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

