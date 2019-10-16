# ftp_benchmark Makefile

CC 	=	g++
INSTALL	=	install
CXXFLAGS	=	-g -lpthread -I. -std=c++11 -fPIC -D_REENTRANT -DNOSSL 

OBJS	=	ftp_benchmark.o ftptest_utils.o ftplib.o WjCryptLib_Md5.o

.cc.o:
	$(CC) -c $*.cc $(CXXFLAGS)

ftp_benchmark: $(OBJS) 
	$(CC) -o ftp_benchmark $(OBJS) $(CXXFLAGS)

install:
	if [ -x /usr/local/sbin ]; then \
		$(INSTALL) -m 755 ftp_benchmark /usr/local/sbin/ftp_benchmark; \
	else \
		$(INSTALL) -m 755 ftp_benchmark /usr/sbin/ftp_benchmark; fi

clean:
	rm -f *.o *.swp ftp_benchmark

