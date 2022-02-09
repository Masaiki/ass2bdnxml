CC=i586-mingw32msvc-gcc
CFLAGS=-O3 -Iinc/ -Wall -DLE_ARCH
LDFLAGS=-lpng -lz -lvfw32 -Llib/ -liberty
OBJS=avs2bdnxml.o auto_split.o palletize.o sup.o sort.o
ASMOBJS=frame-a.o
EXE=avs2bdnxml.exe

%.o: %.c %.h Makefile
	$(CC) -c $< $(CFLAGS)

$(EXE): $(OBJS) $(ASMOBJS)
	$(CC) -o $(EXE) $(OBJS) $(ASMOBJS) $(LDFLAGS)

all: $(EXE)

$(ASMOBJS): $(ASMOBJS:%.o=%.asm)
	yasm -f win32 -m x86 -DARCH_X86_64=0 -DPREFIX=1 $< -o $(<:%.asm=%.o)

dist: clean all
	strip -s $(EXE)
	upx-ucl --best $(EXE)
	rm -f $(OBJS) $(ASMOBJS)

.phony clean:
	rm -f $(EXE) $(OBJS) $(ASMOBJS)

