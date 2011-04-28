#include "beamer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "t20_init.h"

#define VENDOR_ID		0x08ca
#define PRODUCT_ID		0x2137

#define INPUT_EP		1
#define RAW_EP			2
#define COMMAND_EP		3

#define NULL_BULK_LEN		(75*512)

int find_beamer(beamer_handle* beamer) {
	libusb_context *ctx;
	int res;

	res = libusb_init(&ctx);

	libusb_set_debug(ctx, 3);

	*beamer = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
	
	if(*beamer == NULL) {
		return -1;
	}

	res = libusb_claim_interface(*beamer, 0);

	return 0;
}

int finalize_beamer(beamer_handle beamer) {
	libusb_release_interface(beamer, 0);


	libusb_close(beamer);

	return 0;
}

int init_beamer(beamer_handle beamer) {
	int i, transferred;

	for(i = 0; i < sizeof(phase0)/sizeof(phase0[0]); ++i) {
		char* data = phase0[i];
		size_t len = command_length(data);

		libusb_bulk_transfer(beamer, (COMMAND_EP | LIBUSB_ENDPOINT_OUT), (void*) data, len, &transferred, 2000);

		if(data[0] == 0x04) {
			// this type of command seems to need same time to settle ...
			// this might wake up certain parts of the beamer
			// TODO: investigate further!
			// 1ms seems to work too, take 10ms to be sure
			usleep(10 * 1000);
		}
	}

	char nullcmd_data[] = "\x11\x00\x00\x00\x00\xa0\x00\x78\x00\x80\x02\xe0\x01\x00\x10\x00\x10\x04\x00\x96\x00";
	size_t nullcmd_len = 21;

	libusb_bulk_transfer(beamer, (RAW_EP | LIBUSB_ENDPOINT_OUT), (void*) nullcmd_data, nullcmd_len, &transferred, 2000);

	char* null_space = malloc(NULL_BULK_LEN);
	memset(null_space, 0, NULL_BULK_LEN);
	libusb_bulk_transfer(beamer, (RAW_EP | LIBUSB_ENDPOINT_OUT), (void*) null_space, NULL_BULK_LEN, &transferred, 2000);
	free(null_space);

	for(i = 0; i < sizeof(phase1)/sizeof(phase1[0]); ++i) {
		char* data = phase1[i];
		size_t len = command_length(data);

		libusb_bulk_transfer(beamer, (COMMAND_EP | LIBUSB_ENDPOINT_OUT), (void*) data, len, &transferred, 2000);
	}

	return 0;
}

int send_white_image(beamer_handle beamer) {
	int res;
	char* data = malloc(FRAME_SIZE);
	memset(data, 0xff, FRAME_SIZE);
	res = send_raw_image(beamer, data);
	free(data);
	return res;
}

int send_raw_image(beamer_handle beamer, char* data) {
	char* startcmd_data = "\x11\x00\x00\x00\x00\x80\x02\xe0\x01\x80\x02\xe0\x01\x00\x40\x00\x40\x00\x00\x10\x0e";
	size_t startcmd_len = 21;
	int transferred;

	puts("sending frame");

	libusb_bulk_transfer(beamer, (RAW_EP | LIBUSB_ENDPOINT_OUT), (void*) startcmd_data, startcmd_len, &transferred, 2000);

	libusb_bulk_transfer(beamer, (RAW_EP | LIBUSB_ENDPOINT_OUT), (void*) data, FRAME_SIZE, &transferred, 2000);

	return 0;
}

