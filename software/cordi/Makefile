##
## This file is part of the libopencm3 project.
##
## Copyright (C) 2009 Uwe Hermann <uwe@hermann-uwe.de>
##
## This library is free software: you can redistribute it and/or modify
## it under the terms of the GNU Lesser General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## This library is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU Lesser General Public License for more details.
##
## You should have received a copy of the GNU Lesser General Public License
## along with this library.  If not, see <http://www.gnu.org/licenses/>.
##

BINARY = main

LIBLCD = ../liblcd

OBJS =  \
	./fonts/font_courier10.o \
	$(LIBLCD)/liblcd/glib.o \
	$(LIBLCD)/liblcd/eadog.o \
	$(LIBLCD)/liblcd/page.o \
	$(LIBLCD)/liblcd/page_menu.o \
	$(LIBLCD)/liblcd/page_action.o \
	timer.o \
	boom.o \
	gui.o \
	gsm.o \
	ring.o

LIBNAME		= opencm3_stm32f1
DEFS		= -DSTM32F1

CPPFLAGS        = -I$(LIBLCD) -I.

FP_FLAGS	?= -msoft-float
ARCH_FLAGS	= -mthumb -mcpu=cortex-m3 $(FP_FLAGS) -mfix-cortex-m3-ldrd

################################################################################
# OpenOCD specific variables

OOCD		?= openocd
OOCD_INTERFACE	?= flossjtag
OOCD_BOARD	?= olimex_stm32_h103

################################################################################
# Black Magic Probe specific variables
# Set the BMP_PORT to a serial port and then BMP is used for flashing
BMP_PORT	?=

################################################################################
# texane/stlink specific variables
#STLINK_PORT	?= :4242

include ../Makefile.include

