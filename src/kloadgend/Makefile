INCLUDE = $(PWD)/../../include
OBJ     = $(PWD)/../../obj

ccflags-y := -I$(INCLUDE) -O2
obj-m += kloadgend.o

all:
	make -C /lib/modules/`uname -r`/build M=$(OBJ) src=$(PWD) modules

clean:
	make -C /lib/modules/`uname -r`/build M=$(OBJ) clean

