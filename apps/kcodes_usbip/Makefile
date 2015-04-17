KBUILD_VERBOSE	:=1
BOARD_TYPE		:= db12x

KSOURCE			?= ../../$(BOARD_TYPE)/linux/kernels/mips-linux-2.6.31
ARCH			?= mips
CROSS_COMPILE	?= mips-linux-

GPL_NetUSB-objs := GPL_map_NetUSB.o
obj-m :=GPL_NetUSB.o

default:
	make -C $(KSOURCE) ARCH=mips CROSS_COMPILE=mips-linux- LANG=C KBUILD_VERBOSE=${KBUILD_VERBOSE}  M=`pwd` modules

clean:
	-rm -f *.o Module.symvers modules.order GPL_NetUSB.ko GPL_NetUSB.mod.c

