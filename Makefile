# Makefile to build library 'mrpeach' for Pure Data.
# Needs Makefile.pdlibbuilder as helper makefile for platform-dependent build
# settings and rules.

# library name
lib.name = mrpeach

cmos_sources = \
	cmos/cd4000.c \
	cmos/cd4001.c \
	cmos/cd4002.c \
	cmos/cd4008.c \
	cmos/cd4011.c \
	cmos/cd4012.c \
	cmos/cd4013.c \
	cmos/cd4014.c \
	cmos/cd4015.c \
	cmos/cd4017.c \
	cmos/cd40193.c \
	cmos/cd4023.c \
	cmos/cd4024.c \
	cmos/cd4025.c \
	cmos/cd4027.c \
	cmos/cd4070.c \
	cmos/cd4071.c \
	cmos/cd4072.c \
	cmos/cd4073.c \
	cmos/cd4075.c \
	cmos/cd4076.c \
	cmos/cd4081.c \
	cmos/cd4082.c \
	cmos/cd4094.c \
	cmos/cd4516.c \
	$(empty)
binfile_sources = \
	binfile/binfile.c \
	$(empty)
midifile_sources = \
	midifile/midifile.c \
	$(empty)
net_sources = \
	net/udpsend.c \
	net/httpreceive.c \
	net/tcpclient.c \
	net/tcpserver.c \
	net/udpreceive~.c \
	net/udpreceive.c \
	net/httpreq.c \
	net/tcpreceive.c \
	net/udpsndrcv.c \
	net/tcpsend.c \
	net/udpsend~.c \
	$(empty)
osc_sources = \
	osc/unpackOSC.c \
	osc/routeOSC.c \
	osc/packOSC.c \
	osc/pipelist.c \
	$(empty)
slip_sources = \
	slip/slipdec.c \
	slip/slipenc.c \
	$(empty)

cmos_data = \
	cmos/cd4000-help.pd \
	cmos/cd4001-help.pd \
	cmos/cd4002-help.pd \
	cmos/cd4008-help.pd \
	cmos/cd4011-help.pd \
	cmos/cd4012-help.pd \
	cmos/cd4013-help.pd \
	cmos/cd4014-help.pd \
	cmos/cd4015-help.pd \
	cmos/cd4017-help.pd \
	cmos/cd40193-help.pd \
	cmos/cd4023-help.pd \
	cmos/cd4024-help.pd \
	cmos/cd4025-help.pd \
	cmos/cd4027-help.pd \
	cmos/cd4070-help.pd \
	cmos/cd4071-help.pd \
	cmos/cd4072-help.pd \
	cmos/cd4073-help.pd \
	cmos/cd4075-help.pd \
	cmos/cd4076-help.pd \
	cmos/cd4081-help.pd \
	cmos/cd4082-help.pd \
	cmos/cd4094-help.pd \
	cmos/cd4516-help.pd \
	$(empty)
binfile_data = \
	binfile/binfile-help.pd \
	$(empty)
midifile_data = \
	midifile/I_Wanna_Be_Sedated.mid \
	midifile/midifile-help.pd \
	$(empty)
net_data = \
	net/net-meta.pd \
	net/README.txt \
	net/LICENSE.txt \
	net/tcpsend-help.pd \
	net/tcpsocketserver-help.pd \
	net/tcpsocketserver.pd \
	net/httpreceive-help.pd \
	net/tcpclient-help.pd \
	net/tcpserver-help.pd \
	net/tcpreceive-help.pd \
	net/tcpsocket.FUDI-help.pd \
	net/tcpsocket.FUDI.pd \
	net/tcpsocket.OSC-help.pd \
	net/tcpsocket.OSC.pd \
	net/udpreceive~-help.pd \
	net/udpreceive-help.pd \
	net/udpsend~-help.pd \
	net/udpsend-help.pd \
	net/udpsndrcv-help.pd \
	net/httpreq-help.pd \
osc_data = \
	osc/osc-meta.pd \
	osc/README.txt \
	osc/packOSC-help.pd \
	osc/packOSCstream-help.pd \
	osc/packOSCstream.pd \
	osc/pipelist-help.pd \
	osc/unpackOSC-help.pd \
	osc/unpackOSCstream-help.pd \
	osc/unpackOSCstream.pd \
	osc/routeOSC-help.pd \
	$(empty)
slip_data = \
	slip/slipdec-help.pd \
	slip/slipenc-help.pd \
	$(empty)

# input source file (class name == source file basename)
class.sources = \
	$(cmos_sources) \
	$(net_sources) \
	flist2tab/flist2tab.c \
	life2x/life2x.c \
	op~/op~.c \
	xbee/packxbee.c \
	xbee/unpackxbee.c \
	rc~/rc~.c \
	rcosc~/rcosc~.c \
	rojo~/rojo~.c \
	runningmean/runningmean.c \
	serializer/sprint.c \
	serializer/f2b.c \
	serializer/b2f.c \
	sqosc~/sqosc~.c \
	tab2flist/tab2flist.c \
	tabfind/tabfind.c \
	which/which.c \
	$(binfile_sources) \
	$(midifile_sources) \
	$(osc_sources) \
	$(slip_sources) \
	$(empty)

  # pdpigpio/pigpio.c pdpispi/pispi.c pdpii2c/pii2c.c

# all extra files to be included in binary distribution of the library
datafiles = \
	$(cmos_data) \
	$(net_data) \
	flist2tab/flist2tab-help.pd \
	life2x/life2x-help.pd \
	op~/op~-help.pd \
	xbee/bits.pd \
	xbee/packxbee-help.pd \
	xbee/xbeeio.pd \
	xbee/unpackxbee-help.pd \
	xbee/packxbee-example.pd \
	rc~/rc~-help.pd \
	rcosc~/rcosc~-help.pd \
	rojo~/rojo~-help.pd \
	runningmean/runningmean-help.pd \
	serializer/f2b-help.pd \
	serializer/b2f-help.pd \
	serializer/sprint-help.pd \
	sqosc~/sqosc~-help.pd \
	tab2flist/tab2flist-help.pd \
	tabfind/tabfind-help.pd \
	which/which-help.pd \
	$(binfile_data) \
	$(midifile_data) \
	$(osc_data) \
	$(slip_data) \
	$(empty)

 # pdpigpio/pigpio-help.pd pdpispi/pispi-help.pd pdpii2c/pii2c-help.pd
 define forWindows
   ldlibs += -lwsock32 -liphlpapi #-lpthreadGC2 #-lkernel32 -luser32 -lgdi32 -lregex -liberty
 endef

# include Makefile.pdlibbuilder from submodule directory 'pd-lib-builder'
PDLIBBUILDER_DIR=pd-lib-builder/
include $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder
