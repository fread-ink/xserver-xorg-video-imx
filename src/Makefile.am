#  Copyright 2005 Adam Jackson.
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  ADAM JACKSON BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

# this is obnoxious:
# -module lets us name the module exactly how we want
# -avoid-version prevents gratuitous .0.0.0 version numbers on the end
# _ladir passes a dummy rpath to libtool so the thing will actually link
# TODO: -nostdlib/-Bstatic/-lgcc platform magic, not installing the .a, etc.

AM_CPPFLAGS=-I/usr/src/linux/include

NEON_CFLAGS=-march=armv7-a -mfpu=neon -mfloat-abi=hard -Wa,-mfpu=neon

NEON_CCASFLAGS=$(NEON_CFLAGS) -mthumb-interwork
NEON_ASFLAGS=-k -mcpu=cortex-a8 $(NEON_CCASFLAGS)

# Use these two lines to enable Xvideo support
#AM_CFLAGS = @XORG_CFLAGS@ -DRENDER -DCOMPOSITE -DMITSHM -DIMX_XVIDEO_ENABLE=1 $(NEON_CFLAGS)
#imx_drv_la_LDFLAGS = -module -avoid-version -lipu

# Or use these two lines to disable Xvideo support
AM_CFLAGS = @XORG_CFLAGS@ -DRENDER -DCOMPOSITE -DMITSHM -DIMX_XVIDEO_ENABLE=0 $(NEON_CFLAGS)
imx_drv_la_LDFLAGS = -module -avoid-version 

AM_ASFLAGS = $(NEON_ASFLAGS)
AM_CCASFLAGS = $(NEON_CCASFLAGS)

imx_drv_la_LTLIBRARIES = imx_drv.la
imx_drv_ladir = @moduledir@/drivers

ext_HEADERS = imx_ext.h

imx_drv_la_SOURCES = \
	imx.h \
	imx_accel.c \
	imx_accel.h \
	imx_exa.h \
	imx_display.c \
	imx_display.h \
	imx_driver.c \
	imx_ext.c \
	imx_ext.h \
	imx_xv_ipu.c \
	imx_exa_offscreen.c \
	neon_memcpy.S \
	neon_memmove.S
