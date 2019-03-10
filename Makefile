SHELL = bash
OBJS = tiktok.o

CC = m68k-palmos-coff-gcc

CSFLAGS = -O2 -S
CFLAGS = -O2 -g

PILRC = pilrc
TXT2BITM = txt2bitm
OBJRES = m68k-palmos-coff-obj-res
BUILDPRC = build-prc

ICONTEXT = 'TikTok'
APPID = DAP2

all: tiktok.prc

.S.o:
	$(CC) $(TARGETFLAGS) -c $<

.c.s:
	$(CC) $(CSFLAGS) $<

tiktok.prc: code0000.tiktok.bin code0001.tiktok.bin data0000.tiktok.bin bin.res
	$(BUILDPRC) tiktok.prc $(ICONTEXT) $(APPID) code0001.tiktok.grc code0000.tiktok.grc data0000.tiktok.grc *.bin pref0000.tiktok.grc

code0000.tiktok.bin: tiktok
	$(OBJRES) tiktok

code0001.tiktok.bin: code0000.tiktok.bin

data0000.tiktok.bin: code0000.tiktok.bin

bin.res: tiktok.rcp
	$(PILRC) tiktok.rcp .
	$(TXT2BITM) tiktok.pbitm
	touch bin.res

tiktok: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o tiktok

cleanish:
	rm -rf *.[oa] tiktok *.bin bin.res *~ *.grc *.BAK *.bak *.log

clean: cleanish
	rm -rf *.prc

dist: all cleanish
	rm -f ../tiktok.zip
	( cd ../; zip tiktok "tiktok/*" "tiktok/doc/*" )

devdist: all cleanish
	rm -f ../tiktok.zip
	( cd ../; zip -r tiktok "tiktok/*" )