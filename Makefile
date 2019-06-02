.POSIX:

INCLUDE   = $(CURDIR)/include
SRC       = $(CURDIR)/src
LOADGEND  = $(SRC)/loadgend
KLOADGEND = $(SRC)/kloadgend
TOOLS     = $(SRC)/tools
OBJ       = $(CURDIR)/obj
PREFIX    = /usr/local
CC        = gcc
CXX       = g++
CFLAGS    = -O2 -g
CXXFLAGS  = $(CFLAGS)
CPPFLAGS  = -DLOADGEND_SOURCE=1  -I$(INCLUDE)
LDFLAGS   = -lpthread -lrt -lm

all: loadgend kloadgend loadgenctl measurer
loadgend: $(OBJ)/loadgend
kloadgend: $(OBJ)/kloadgend.ko
loadgenctl: $(OBJ)/loadgenctl
measurer: $(OBJ)/measurer

$(OBJ)/loadgend: $(OBJ)/loadgend.o $(OBJ)/loadgen_sysload.o \
                 $(OBJ)/loadgen_unix.o $(OBJ)/loadgen_netlink.o \
                 $(OBJ)/loadgen_thread.o
	@ mkdir -p $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(OBJ)/loadgend.o \
		$(OBJ)/loadgen_sysload.o $(OBJ)/loadgen_unix.o \
        $(OBJ)/loadgen_netlink.o $(OBJ)/loadgen_thread.o $(LDFLAGS)

$(OBJ)/loadgend.o: $(LOADGEND)/loadgend.c \
                   $(INCLUDE)/loadgen_sysload.h \
                   $(INCLUDE)/loadgen_conf.h \
                   $(INCLUDE)/loadgen_log.h \
                   $(INCLUDE)/loadgen_thread.h \
                   $(INCLUDE)/loadgen_unix.h \
                   $(INCLUDE)/loadgen_netlink.h \
                   $(INCLUDE)/loadgen_cpuload.h
	@ mkdir -pv $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/loadgen_sysload.o: $(LOADGEND)/loadgen_sysload.c \
                          $(INCLUDE)/loadgen_sysload.h \
                          $(INCLUDE)/loadgen_log.h
	@ mkdir -pv $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/loadgen_thread.o: $(LOADGEND)/loadgen_thread.c \
                         $(INCLUDE)/loadgen_sysload.h \
                         $(INCLUDE)/loadgen_thread.h \
                         $(INCLUDE)/loadgen_log.h
	@ mkdir -pv $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/loadgen_unix.o: $(LOADGEND)/loadgen_unix.c \
                       $(INCLUDE)/loadgen_sysload.h \
                       $(INCLUDE)/loadgen_unix.h \
                       $(INCLUDE)/loadgen_netlink.h \
                       $(INCLUDE)/loadgen_log.h \
                       $(INCLUDE)/loadgen_thread.h \
                       $(INCLUDE)/loadgen_cpuload.h
	@ mkdir -pv $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/loadgen_netlink.o: $(LOADGEND)/loadgen_netlink.c \
                          $(INCLUDE)/loadgen_log.h \
                          $(INCLUDE)/loadgen_sysload.h \
                          $(INCLUDE)/loadgen_netlink.h \
                          $(INCLUDE)/loadgen_cpuload.h
	@ mkdir -pv $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/kloadgend.ko: $(KLOADGEND)/kloadgend.c $(INCLUDE)/loadgen_netlink.h \
                     $(INCLUDE)/loadgen_cpuload.h
	@ mkdir -pv $(OBJ)
	@ touch $(OBJ)/Makefile
	@ cd $(KLOADGEND) && make

$(OBJ)/loadgenctl: $(TOOLS)/loadgenctl.cc \
                   $(INCLUDE)/loadgen_conf.h \
                   $(INCLUDE)/loadgen_unix.h
	@ mkdir -pv $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $<

$(OBJ)/measurer: $(TOOLS)/measurer.cc
	@ mkdir -pv $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $<

install: loadgend kloadgend
	@ mkdir -pv $(DESTDIR)$(PREFIX)/bin
	@ mkdir -pv $(DESTDIR)/lib/modules/`uname -r`
	@ ln -svf $(OBJ)/loadgend $(DESTDIR)$(PREFIX)/bin/loadgend
	@ ln -svf $(OBJ)/kloadgend.ko $(DESTDIR)/lib/modules/`uname -r`

uninstall:
	@ rm -vf $(DESTDIR)$(PREFIX)/bin/loadgend
	@ rm -vf $(DESTDIR)/lib/modules/`uname -r`/kloadgend.ko

clean:
	@ if test -d $(OBJ); then \
		touch $(OBJ)/Makefile; \
		cd $(KLOADGEND) && make clean; \
	fi
	@ rm -vf $(OBJ)/loadgend $(OBJ)/loadgend.o $(OBJ)/loadgen_thread.o \
             $(OBJ)/loadgen_unix.o $(OBJ)/loadgenctl $(OBJ)/measurer \
             $(OBJ)/Makefile

distclean:
	@ rm -rfv $(OBJ)
