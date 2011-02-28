/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * main.c
 * Copyright (C) John Stone 2009 <johnättuxcodetorg>
 * 
 * usbnarf is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * usbnarf is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>

#include "options.h"
#include "debugutil.h"

#include "usbnarf.h"

static int 
usbnarf_init(struct cud *cd);

static int 
usbnarf_exit(struct cud *cd);

static inline int
usbnarf_attach(struct cud *cd);

static inline int
usbnarf_detach(struct cud *cd);

static int
usbnarf_readoptions(struct cud *cd);

int 
usbnarf_perform_transfer(struct cud *cd, const char ep,
		size_t numbytes,void * data);

int
main ( int argc, const char * argv[]  )
{
	int i;
	struct cud cud;
	char *dta;
	
	struct usbnarf_conf  theoptions;

	i = parser(argc,argv,&theoptions);

	dta = calloc(120,1);

	// usbnarf_readoptions(&cud);
	//
	
	i = usbnarf_init(&cud);
	debugf("init done! ret: %d",i);
	i= usbnarf_attach(&cud);
	debugf("Attached! ret: %d",i);

	if (!i) 
		usbnarf_perform_transfer(&cud,0x2,64,dta);
	debugf("exiting!");	
	return usbnarf_exit(&cud);
}


static int 
usbnarf_init(struct cud *cd)
{
	int i;
	i = libusb_init(&(cd->uctx));
	cd->duh=NULL;
	debugf("libusb init done! ret: %d",i);
	libusb_set_debug(cd->uctx , 3); //3 means full debug info
	
	return 0;
}



/**
 * process proram options
 */
static int
usbnarf_readoptions(struct cud *cd)
{
	cd->duh_vendor = theoptions.duh_vendor;
	cd->duh_product = theoptions.duh_product;

	/*FIXME do rest of option processing*/
	return 0;
}


/*
 * regular program termination
 */
static int 
usbnarf_exit(struct cud *cd)
{
	int i =0;
	
	if (cd->duh)
	{
		debugf("detaching...");
		usbnarf_detach(cd);
	}

	libusb_exit(cd->uctx);

	return i;
}

/*
 * attach to the duh
 * TODO: look if there is a kernel driver ...
 */
static inline int
usbnarf_attach(struct cud *cd)
{
	if (cd->duh)
	{
		debugf("Cannot attach!Already dealing with one device.");
		return -1;
	}
	cd->duh = libusb_open_device_with_vid_pid (
			cd->uctx,cd->duh_vendor,cd->duh_product);
	if(!cd->duh)
	{
		debugf("Cannot attach! Specifyed device unopenable.");
		return -1;
	}
	return 0;
}

static inline int
usbnarf_detach(struct cud *cd)
{
	if(!cd->duh)
	{
		debugf("Cannot detach!theres no attached device!");
		return -1;
	}

	libusb_close(cd->duh);
	cd->duh = NULL;
	return 0;
}



int usbnarf_perform_transfer(struct cud *cd, const char ep, size_t numbytes,void * data)
{
	int i,j;
	j=0;


	i = libusb_bulk_transfer ( cd->duh, 	/*our device under hack*/
				ep,		/*endpoint*/
		(unsigned int *) data ,		/*...just it*/
				numbytes,	/*wanna xfer*/
				&j,		/*xfered*/
				0		/*timeout*/);

	debugf("xfered %d bytes!",j);
	return i;
}




