#ifndef __USBNARF_H_
#define __USBNARF_H_
/*
 * ============================================================================
 *
 *       Filename:  usbnarf.h
 *
 *        Version:  1.0
 *        Created:  12.12.2009 02:08:29
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:   johnättuxcodedotorg
 *
 * ============================================================================
 */

#include <libusb-1.0/libusb.h>

/*central usbnarf data*/
struct cud { 
	libusb_context *uctx;
	/*decribing the device under hack:*/
	uint16_t duh_vendor;
	uint16_t duh_product;

	libusb_device_handle *duh;

};

#endif

