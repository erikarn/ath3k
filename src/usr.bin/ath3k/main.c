/*-
 * Copyright (c) 2013 Adrian Chadd <adrian@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <err.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libusb.h>

#include "ath3k_fw.h"
#include "ath3k_hw.h"
#include "ath3k_dbg.h"

#define	_DEFAULT_ATH3K_FIRMWARE_PATH	"/usr/share/firmware/ath3k/"

int	ath3k_do_debug = 0;
int	ath3k_do_info = 1;

libusb_device *
ath3k_find_device(libusb_context *ctx, int bus_id, int dev_id)
{
	libusb_device **list, *dev = NULL, *found = NULL;
	ssize_t cnt, i;
	int err = 0;

	cnt = libusb_get_device_list(ctx, &list);
	if (cnt < 0) {
		ath3k_err("%s: libusb_get_device_list() failed: code %d\n",
		    __func__,
		    cnt);
		return (NULL);
	}

	/*
	 * XXX TODO: match on the vendor/product id too!
	 */
	for (i = 0; i < cnt; i++) {
		dev = list[i];
		if (bus_id == libusb_get_bus_number(dev) &&
		    dev_id == libusb_get_device_address(dev)) {
			/*
			 * Take a reference so it's not freed later on.
			 */
			found = libusb_ref_device(dev);
			break;
		}
	}

	libusb_free_device_list(list, 1);
	return (found);
}

static int
ath3k_init_ar3012(libusb_device_handle *hdl, const char *fw_path)
{
	int ret;

	ret = ath3k_load_patch(hdl, fw_path);
	if (ret < 0) {
		ath3k_err("Loading patch file failed\n");
	return (ret);
	}

	ret = ath3k_load_syscfg(hdl, fw_path);
	if (ret < 0) {
		ath3k_err("Loading sysconfig file failed\n");
		return (ret);
	}

	ret = ath3k_set_normal_mode(hdl);
	if (ret < 0) {
		ath3k_err("Set normal mode failed\n");
		return (ret);
	}

	ath3k_switch_pid(hdl);
	return (0);
}

static int
ath3k_init_firmware(libusb_device_handle *hdl, const char *file_prefix)
{
	struct ath3k_firmware fw;
	char fwname[FILENAME_MAX];
	int ret;

	/* XXX path info? */
	snprintf(fwname, FILENAME_MAX, "%s/ath3k-1.fw", file_prefix);

	ath3k_debug("%s: loading ath3k-1.fw\n", __func__);

	/* Read in the firmware */
	if (ath3k_fw_read(&fw, fwname) <= 0) {
		fprintf(stderr, "%s: ath3k_fw_read() failed\n",
		    __func__);
		return (-1);
	}

	/* Load in the firmware */
	ret = ath3k_load_fwfile(hdl, &fw);

	/* free it */
	ath3k_fw_free(&fw);

	return (0);
}

/*
 * Parse ugen name and extract device's bus and address
 */

static int
parse_ugen_name(char const *ugen, uint8_t *bus, uint8_t *addr)
{
	char *ep;

	if (strncmp(ugen, "ugen", 4) != 0)
		return (-1);

	*bus = (uint8_t) strtoul(ugen + 4, &ep, 10);
	if (*ep != '.')
		return (-1);

	*addr = (uint8_t) strtoul(ep + 1, &ep, 10);
	if (*ep != '\0')
		return (-1);

	return (0);
}

static void
usage(void)
{
	fprintf(stderr,
	    "Usage: ath3kfw -d ugenX.Y -f firmware path (-m <ar3012>)"
	    " -p <product id> -v <vandor id>\n");
	exit(127);
}

int
main(int argc, char *argv[])
{
	libusb_context *ctx;
	libusb_device *dev;
	libusb_device_handle *hdl;
	unsigned char state;
	struct ath3k_version ver;
	int r;
	uint16_t dev_device_id = 0, dev_vendor_id = 0;
	uint8_t bus_id = 0, dev_id = 0;
	int n;
	char *firmware_path = NULL;
	int is_3012 = 0;

	/* libusb setup */
	r = libusb_init(&ctx);
	if (r != 0) {
		ath3k_err("%s: libusb_init failed: code %d\n",
		    argv[0],
		    r);
		exit(127);
	}

	/* Enable debugging, just because */
	libusb_set_debug(ctx, 3);

	/* Parse command line arguments */
	while ((n = getopt(argc, argv, "d:f:hm:p:v:")) != -1) {
		switch (n) {
		case 'd': /* ugen device name */
			if (parse_ugen_name(optarg, &bus_id, &dev_id) < 0)
				usage();
			break;

		case 'f': /* firmware path */
			if (firmware_path)
				free(firmware_path);
			firmware_path = strdup(optarg);
			break;
		case 'm':
			if (strcmp(optarg, "ar3012") == 0) {
				is_3012 = 1;
			}
			break;
		case 'p': /* product id */
			dev_device_id = strtol(optarg, NULL, 0);
			break;
		case 'v': /* vendor id */
			dev_vendor_id = strtol(optarg, NULL, 0);
			break;
		case 'h':
		default:
			usage();
			break;
			/* NOT REACHED */
		}
	}

	ath3k_debug("%s: opening dev %d.%d\n",
	    basename(argv[0]),
	    (int) bus_id,
	    (int) dev_id);

	/* Find a device based on the bus/dev id */
	dev = ath3k_find_device(ctx, bus_id, dev_id);
	if (dev == NULL) {
		ath3k_err("%s: device not found\n", __func__);
		/* XXX cleanup? */
		exit(1);
	}

	/* XXX enforce that bInterfaceNumber is 0 */

	/* Grab device handle */
	r = libusb_open(dev, &hdl);
	if (r != 0) {
		ath3k_err("%s: libusb_open() failed: code %d\n", __func__, r);
		/* XXX cleanup? */
		exit(1);
	}

	/*
	 * Get the initial NIC state.
	 */
	r = ath3k_get_state(hdl, &state);
	if (r == 0) {
		ath3k_err("%s: ath3k_get_state() failed!\n", __func__);
		/* XXX cleanup? */
		exit(1);
	}
	ath3k_debug("%s: state=0x%02x\n",
	    __func__,
	    (int) state);

	/* And the version */
	r = ath3k_get_version(hdl, &ver);
	if (r == 0) {
		ath3k_err("%s: ath3k_get_version() failed!\n", __func__);
		/* XXX cleanup? */
		exit(1);
	}
	ath3k_info("ROM version: %d, build version: %d, ram version: %d, "
	    "ref clock=%d\n",
	    ver.rom_version,
	    ver.build_version,
	    ver.ram_version,
	    ver.ref_clock);

	/* Default the firmware path */
	if (firmware_path == NULL)
		firmware_path = strdup(_DEFAULT_ATH3K_FIRMWARE_PATH);

	if (is_3012) {
		(void) ath3k_init_ar3012(hdl, firmware_path);
	} else {
		(void) ath3k_init_firmware(hdl, firmware_path);
	}

	/* Shutdown */
	libusb_close(hdl);
	hdl = NULL;

	libusb_unref_device(dev);
	dev = NULL;

	libusb_exit(ctx);
	ctx = NULL;
}
