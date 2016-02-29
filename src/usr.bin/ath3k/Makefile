# $FreeBSD$

CFLAGS+=	-g
PROG=		ath3kfw
#MAN=		ath3kfw.8
DPADD+=		${LIBUSB}
LDADD+=		-lusb
NO_MAN=		yes
SRCS=		main.c ath3k_fw.c ath3k_hw.c

.include <bsd.prog.mk>
