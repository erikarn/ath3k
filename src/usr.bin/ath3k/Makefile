# $FreeBSD: head/usr.sbin/bluetooth/ath3kfw/Makefile 222772 2011-06-06 20:24:17Z ed $

CFLAGS+=	-g
PROG=		ath3k
#MAN=		ath3k.8
DPADD+=		${LIBUSB}
LDADD+=		-lusb
NO_MAN=		yes
SRCS=		main.c ath3k_fw.c ath3k_hw.c

.include <bsd.prog.mk>
