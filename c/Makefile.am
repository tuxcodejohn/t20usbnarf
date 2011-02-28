## Process this file with automake to produce Makefile.in
## Created by Anjuta

SUBDIRS = src 

usbnarfdocdir = ${prefix}/doc/usbnarf
usbnarfdoc_DATA = \
	README\
	COPYING\
	AUTHORS\
	ChangeLog\
	INSTALL\
	NEWS


EXTRA_DIST = $(usbnarfdoc_DATA)


# Remove doc directory on uninstall
uninstall-local:
	-rm -r $(usbnarfdocdir)
