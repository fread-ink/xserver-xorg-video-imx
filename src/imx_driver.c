/*
 * Copyright (C) 2009-2011 Freescale Semiconductor, Inc.  All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation 
 * files (the "Software"), to deal in the Software without 
 * restriction, including without limitation the rights to use, copy, 
 * modify, merge, publish, distribute, sublicense, and/or sell copies 
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS 
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN 
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN 
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
 * SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <linux/fb.h>
#include <linux/mxcfb.h>

#include "xf86.h"
#include "xf86_OSproc.h"

#include "mipointer.h"
#include "micmap.h"
#include "colormapst.h"
#include "xf86cmap.h"
#include "exa.h"

/* for visuals */
#include "fb.h"
#include "fbdevhw.h"

#include "compat-api.h"

#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#include "xf86RAC.h"
#endif


#include "imx.h"
#include "imx_display.h"

#if IMX_XVIDEO_ENABLE
#include "xf86xv.h"
#endif


#define IMX_NAME		"imx"
#define IMX_DRIVER_NAME		"imx"

#define IMX_VERSION_MAJOR	PACKAGE_VERSION_MAJOR
#define IMX_VERSION_MINOR	PACKAGE_VERSION_MINOR
#define IMX_VERSION_PATCH	PACKAGE_VERSION_PATCHLEVEL

#define IMX_VERSION_CURRENT \
	((IMX_VERSION_MAJOR << 20) |\
	(IMX_VERSION_MINOR << 10) | \
	 (IMX_VERSION_PATCH))


#if IMX_XVIDEO_ENABLE
/* for Xvideo */
extern int MXXVInitializeAdaptor(ScrnInfoPtr, XF86VideoAdaptorPtr **);
#endif

/* For X extension */
extern void imxExtInit();



/* -------------------------------------------------------------------- */

/* Supported "chipsets" */
static SymTabRec imxChipsets[] = {
    { 0, "i.MX5x Z160" },
    {-1, NULL }
};

/* Supported options */
typedef enum {
	OPTION_FBDEV,
	OPTION_FORMAT_EPDC,
	OPTION_NOACCEL,
	OPTION_ACCELMETHOD
} IMXOpts;

#define	OPTION_STR_FBDEV	"fbdev"
#define	OPTION_STR_FORMAT_EPDC	"FormatEPDC"
#define	OPTION_STR_NOACCEL	"NoAccel"
#define	OPTION_STR_ACCELMETHOD	"AccelMethod"

static const OptionInfoRec imxOptions[] = {
	{ OPTION_FBDEV,		OPTION_STR_FBDEV,	OPTV_STRING,	{0},	FALSE },
	{ OPTION_FORMAT_EPDC,	OPTION_STR_FORMAT_EPDC,	OPTV_STRING,	{0},	FALSE },
	{ OPTION_NOACCEL,	OPTION_STR_NOACCEL,	OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_ACCELMETHOD,	OPTION_STR_ACCELMETHOD,	OPTV_STRING,	{0},	FALSE },
	{ -1,			NULL,			OPTV_NONE,	{0},	FALSE }
};

/* -------------------------------------------------------------------- */

static void
imxFreeRec(ScrnInfoPtr pScrn)
{
	if (NULL != pScrn->driverPrivate) {

		ImxPtr fPtr = IMXPTR(pScrn);
		if (NULL != fPtr->pOptions) {
			free(fPtr->pOptions);
		}

		free(pScrn->driverPrivate);

		pScrn->driverPrivate = NULL;
	}
}

static ImxPtr
imxGetRec(ScrnInfoPtr pScrn)
{
	ImxPtr fPtr = NULL;

	if (NULL == pScrn->driverPrivate) {
	
		pScrn->driverPrivate = calloc(sizeof(ImxRec), 1);
		if (NULL == pScrn->driverPrivate) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   	"unable to allocate driver private memory\n");
			return NULL;
		}

		fPtr = IMXPTR(pScrn);
		fPtr->useAccel = FALSE;

		if (NULL == (fPtr->pOptions = malloc(sizeof(imxOptions)))) {
			imxFreeRec(pScrn);
			return NULL;
		}
	}

	return fPtr;
}

/* -------------------------------------------------------------------- */

static Bool
imxPreInitEPDC(ScrnInfoPtr pScrn, ImxPtr fPtr, int fd, char* strFormat)
{
	Bool result = TRUE;

	/* Get frame buffer variable screen info */
	struct fb_var_screeninfo fbVarScreenInfo;
	if (-1 == ioctl(fd,FBIOGET_VSCREENINFO,(void*)(&fbVarScreenInfo))) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "FBIOGET_VSCREENINFO: %s\n", strerror(errno));
		return FALSE;
	}

	/* Find the requested EPDC format and change if requested */
	if (NULL != strFormat) {
		if (0 == xf86NameCmp(strFormat, "RGB565")) {
			fbVarScreenInfo.grayscale = 0;
			fbVarScreenInfo.bits_per_pixel = 16;
		}
		else if (0 == xf86NameCmp(strFormat, "Y8")) {
			fbVarScreenInfo.grayscale = GRAYSCALE_8BIT;
			fbVarScreenInfo.bits_per_pixel = 8;
		}
		else if (0 == xf86NameCmp(strFormat, "Y8INV")) {
			fbVarScreenInfo.grayscale = GRAYSCALE_8BIT_INVERTED;
			fbVarScreenInfo.bits_per_pixel = 8;
		}
		else {
			xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
				"\"%s\" is not a valid value for Option \"%s\"\n", strFormat, OPTION_STR_FORMAT_EPDC);
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,
				"valid options are \"RGB565\", \"Y8\" and \"Y8INV\"\n");
			return FALSE;
		}
	}

	/* It is required to initialize the device so */
	/* use force activation just in case nothing changed */
	fbVarScreenInfo.activate = FB_ACTIVATE_FORCE;
	if (-1 == ioctl(fd,FBIOPUT_VSCREENINFO,(void*)(&fbVarScreenInfo))) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "FBIOPUT_VSCREENINFO: %s\n", strerror(errno));
		return FALSE;
	}

	return TRUE;
}

static Bool
imxPreInit(ScrnInfoPtr pScrn, int flags)
{
	int default_depth, fbbpp;
	const char *s;
	int type;

	/* Do not auto probe */
	if (flags & PROBE_DETECT) {
		return FALSE;
	}

	/* Check the number of entities, and fail if it isn't one. */
	if (pScrn->numEntities != 1) {
		return FALSE;
	}

	/* Load the frame buffer module. */
	if (xf86LoadSubModule(pScrn, "fb") == NULL) {
		return FALSE;
	}

	/* Use the current monitor set in xorg.conf. */
	/* X needs this to be set. */
	pScrn->monitor = pScrn->confScreen->monitor;

	/* Allocate driver private data associated with screen. */
	ImxPtr fPtr = imxGetRec(pScrn);
	if (NULL == fPtr) {
		return FALSE;
	}

	fPtr->pEntity = xf86GetEntityInfo(pScrn->entityList[0]);

	/* Access the name of the frame buffer device */
	char* fbdevName = xf86FindOptionValue(fPtr->pEntity->device->options, OPTION_STR_FBDEV);
	if (NULL == fbdevName) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"Option \"%s\" missing\n", OPTION_STR_FBDEV);
		goto errorPreInit;
	}

	/* Open the frame buffer device */
	int fd = open(fbdevName,O_RDWR,0);
	if (fd == -1) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"open %s: %s\n", fbdevName, strerror(errno));
		goto errorPreInit;
	}

	/* Get frame buffer fixed screen info */
	struct fb_fix_screeninfo fbFixScreenInfo;
	if (-1 == ioctl(fd,FBIOGET_FSCREENINFO,(void*)(&fbFixScreenInfo))) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "FBIOGET_FSCREENINFO: %s\n", strerror(errno));
		close(fd);
		goto errorPreInit;
	}

	/* Same the frame buffer driver name and the device name. */
	strcpy(fPtr->fbId, fbFixScreenInfo.id);
	strcpy(fPtr->fbDeviceName, fbdevName + 5); // skip past "/dev/"

	/* Special case for EPDC driver */
	if (ImxFbTypeEPDC == imxDisplayGetFrameBufferType(&fbFixScreenInfo)) {

		/* Get the format for the EPDC */
		char* strFormat = xf86FindOptionValue(fPtr->pEntity->device->options, OPTION_STR_FORMAT_EPDC);

		/* Perform pre-init on EPDC */
		if (!imxPreInitEPDC(pScrn, fPtr, fd, strFormat)) {
			close(fd);
			goto errorPreInit;
		}
	}
	close(fd);

	/* Open device */
	if (!fbdevHWInit(pScrn,NULL,fbdevName)) {
		goto errorPreInit;
	}
	default_depth = fbdevHWGetDepth(pScrn,&fbbpp);
	if (!xf86SetDepthBpp(pScrn, default_depth, default_depth, fbbpp,
			     Support24bppFb | Support32bppFb | SupportConvert32to24 | SupportConvert24to32)) {
		goto errorPreInit;
	}
	xf86PrintDepthBpp(pScrn);

	/* Color weight */
	if (pScrn->depth > 8) {
		rgb zeros = { 0, 0, 0 };
		if (!xf86SetWeight(pScrn, zeros, zeros)) {
			goto errorPreInit;
		}
	}

	/* Visual init */
	if (!xf86SetDefaultVisual(pScrn, -1)) {
		goto errorPreInit;
	}

	/* We don't currently support DirectColor at > 8bpp */
	if (pScrn->depth > 8 && pScrn->defaultVisual != TrueColor) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "requested default visual"
			   " (%s) is not supported at depth %d\n",
			   xf86GetVisualName(pScrn->defaultVisual), pScrn->depth);
		goto errorPreInit;
	}

	{
		Gamma zeros = {0.0, 0.0, 0.0};

		if (!xf86SetGamma(pScrn,zeros)) {
			goto errorPreInit;
		}
	}

	pScrn->progClock = TRUE;
	pScrn->rgbBits   = 8;
	pScrn->chipset   = IMX_DRIVER_NAME;
	pScrn->videoRam  = fbdevHWGetVidmem(pScrn);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hardware: %s (video memory:"
		   " %dkB)\n", fbdevHWGetName(pScrn), pScrn->videoRam/1024);

	/* Handle options */
	xf86CollectOptions(pScrn, NULL);
	memcpy(fPtr->pOptions, imxOptions, sizeof(imxOptions));
	xf86ProcessOptions(pScrn->scrnIndex, fPtr->pEntity->device->options, fPtr->pOptions);

	/* NoAccel option */
  fPtr->useAccel = FALSE;

	/* AccelMethod option */

	/* Display pre-init */
	if (!imxDisplayPreInit(pScrn)) {

		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"error pre-initializing display\n");
		goto errorPreInit;
	}

	/* Set display resolution */
	xf86SetDpi(pScrn, 0, 0);

	/* Load bpp-specific modules */
	switch ((type = fbdevHWGetType(pScrn)))
	{
	case FBDEVHW_PLANES:
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "plane mode is not supported by the imx driver\n");
		goto errorPreInit;
		break;
	case FBDEVHW_PACKED_PIXELS:
		switch (pScrn->bitsPerPixel)
		{
		case 8:
		case 16:
		case 24:
		case 32:
			break;
		default:
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"unsupported number of bits per pixel: %d",
			pScrn->bitsPerPixel);
			goto errorPreInit;
		}
		break;
	case FBDEVHW_INTERLEAVED_PLANES:
               /* Not supported yet, don't know what to do with this */
               xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "interleaved planes are not yet supported by the "
			  "imx driver\n");
		goto errorPreInit;
	case FBDEVHW_TEXT:
               /* This should never happen ...
                * we should check for this much much earlier ... */
               xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "text mode is not supported by the fbdev driver\n");
		goto errorPreInit;
       case FBDEVHW_VGA_PLANES:
               /* Not supported yet */
               xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "EGA/VGA planes are not yet supported by the imx "
			  "driver\n");
		goto errorPreInit;
       default:
               xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "unrecognised imx hardware type (%d)\n", type);
		goto errorPreInit;
	}

	return TRUE;

errorPreInit:
	imxFreeRec(pScrn);
	return FALSE;
}

static void
imxFreeScreen(FREE_SCREEN_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);

	imxFreeRec(pScrn);
}

static Bool
imxCloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
	CLOSE_SCREEN_DECL_ScrnInfoPtr;
	ImxPtr fPtr = IMXPTR(pScrn);

	fbdevHWRestore(pScrn);
	fbdevHWUnmapVidmem(pScrn);
	pScrn->vtSema = FALSE;

	pScreen->CloseScreen = fPtr->saveCloseScreen;
	return (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
}

static int
GCD(int a, int b)
{
	/* Euclidean's algorithm */

	if (0 == a)
	{
		return b;
	}

	while (0 != b)
	{
		if (a > b)
		{
			a -= b;
		}
		else
		{
			b -= a;
		}
	}

	return a;
}

static int
LCM(a, b)
{
	return (a * b) / GCD(a, b);
}

static Bool
imxScreenInit(SCREEN_INIT_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	ImxPtr fPtr = IMXPTR(pScrn);
	VisualPtr visual;
	int init_picture = 0;
	int ret, flags;
	int type;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"bitsPerPixel=%d depth=%d defaultVisual=%s\n",
		pScrn->bitsPerPixel,
		pScrn->depth,
		xf86GetVisualName(pScrn->defaultVisual));
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"rgbOffset=%d,%d,%d rgbMask=0x%08x,0x%08x,0x%08x\n",
		(int)(pScrn->offset.red),
		(int)(pScrn->offset.green),
		(int)(pScrn->offset.blue),
		(int)(pScrn->mask.red),
		(int)(pScrn->mask.green),
		(int)(pScrn->mask.blue));

	/* Map frame buffer memory */
	fPtr->fbMemoryBase = fbdevHWMapVidmem(pScrn);
	if (NULL == fPtr->fbMemoryBase) {
	        xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"mapping of video memory"
			   " failed\n");
		return FALSE;
	}
	fPtr->fbMemoryOffset = fbdevHWLinearOffset(pScrn);
	fPtr->fbMemorySize = fbdevHWGetVidmem(pScrn) - fPtr->fbMemoryOffset;
	fPtr->fbMemoryStart = fPtr->fbMemoryBase + fPtr->fbMemoryOffset;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"physAddr=0x%08x fbStart=0x%08x = 0x%08x + 0x%08x\n",
		(int)(pScrn->memPhysBase),
		(int)(fPtr->fbMemoryStart),
		(int)(fPtr->fbMemoryBase),
		(int)(fPtr->fbMemoryOffset));

	/* TODO - established properties only for mx5x */
	fPtr->fbAlignOffset = 4096;
	fPtr->fbAlignWidth = 32;
	fPtr->fbAlignHeight = 32;

	/* Retrieve the max sizes supported by frame buffer. */
	int fbMaxWidth;
	int fbMaxHeight;
	imxDisplayGetPreInitMaxSize(pScrn, &fbMaxWidth, &fbMaxHeight);

	/* Apply alignment requirements */
	fbMaxWidth = IMX_ALIGN(fbMaxWidth, fPtr->fbAlignWidth);
	fbMaxHeight = IMX_ALIGN(fbMaxHeight, fPtr->fbAlignHeight);

	/* What is aligned bytes per line? */
	const int fbBytesPerPixel = (pScrn->bitsPerPixel + 7) / 8;
	const int fbBytesPerLine = fbMaxWidth * fbBytesPerPixel;

	/* Compute the offset alignment which is least common multiple */
	/* (LCM) of vertical pixel alignment combined with bytes per line */
	/* versus the offset alignment. */
	const int fbMaxAlignOffset =
		LCM(fbBytesPerLine * fPtr->fbAlignHeight, fPtr->fbAlignOffset);

	/* Determine if there is enough memory to reserve a */
	/* second frame buffer for XRandR rotation support. */
	const int fbMaxScreenSize = fbMaxWidth * fbMaxHeight * fbBytesPerPixel;
	const int fbOffsetScreen2 =
		IMX_ALIGN(fbMaxScreenSize, fbMaxAlignOffset);
	fPtr->fbMemoryScreenReserve = fbMaxScreenSize;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"reserve %d bytes of frame buffer for screen\n",
		fPtr->fbMemoryScreenReserve);
	fPtr->fbMemoryStart2 = NULL;
	if ((fbOffsetScreen2 + fbMaxScreenSize) < fPtr->fbMemorySize) {

		fPtr->fbMemoryStart2 = fPtr->fbMemoryStart + fbOffsetScreen2;

		fPtr->fbMemoryScreenReserve += fbOffsetScreen2;

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			"reserve same number of bytes for XRandR rotated screen at offset %d\n",
			fbOffsetScreen2);
	}

	if (!imxDisplayStartScreenInit(pScrn->scrnIndex, pScreen)) {

		return FALSE;
	}

	/* mi layer */
	miClearVisualTypes();
	if (pScrn->bitsPerPixel > 8) {
		if (!miSetVisualTypes(pScrn->depth, TrueColorMask, pScrn->rgbBits, TrueColor)) {
			xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"visual type setup failed"
				   " for %d bits per pixel [1]\n",
				   pScrn->bitsPerPixel);
			return FALSE;
		}
	} else {
		if (!miSetVisualTypes(pScrn->depth,
				      miGetDefaultVisualMask(pScrn->depth),
				      pScrn->rgbBits, pScrn->defaultVisual)) {
			xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"visual type setup failed"
				   " for %d bits per pixel [2]\n",
				   pScrn->bitsPerPixel);
			return FALSE;
		}
	}
	if (!miSetPixmapDepths()) {
	  xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"pixmap depth setup failed\n");
	  return FALSE;
	}

#if 0
	/* FIXME: this doesn't work for all cases, e.g. when each scanline
		has a padding which is independent from the depth (controlfb) */
	pScrn->displayWidth = fbdevHWGetLineLength(pScrn) /
			      (pScrn->bitsPerPixel / 8);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "displayWidth = %d\n", pScrn->displayWidth);

	if (pScrn->displayWidth != pScrn->virtualX) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "Pitch updated to %d after ModeInit\n",
			   pScrn->displayWidth);
	}
#endif


	switch ((type = fbdevHWGetType(pScrn)))
	{
	case FBDEVHW_PACKED_PIXELS:
		switch (pScrn->bitsPerPixel) {
		case 8:
		case 16:
		case 24:
		case 32:
			ret = fbScreenInit(pScreen, fPtr->fbMemoryStart,
					   pScrn->virtualX, pScrn->virtualY,
					   pScrn->xDpi, pScrn->yDpi,
					   pScrn->displayWidth,
					   pScrn->bitsPerPixel);
			init_picture = 1;
			break;
	 	default:
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "internal error: invalid number of bits per"
				   " pixel (%d) encountered in"
				   " imxScreenInit()\n", pScrn->bitsPerPixel);
			ret = FALSE;
			break;
		}
		break;
	case FBDEVHW_INTERLEAVED_PLANES:
		/* This should never happen ...
		* we should check for this much much earlier ... */
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: interleaved planes are not yet "
			   "supported by the imx driver\n");
		ret = FALSE;
		break;
	case FBDEVHW_TEXT:
		/* This should never happen ...
		* we should check for this much much earlier ... */
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: text mode is not supported by the "
			   "imx driver\n");
		ret = FALSE;
		break;
	case FBDEVHW_VGA_PLANES:
		/* Not supported yet */
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: EGA/VGA Planes are not yet "
			   "supported by the imx driver\n");
		ret = FALSE;
		break;
	default:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: unrecognised hardware type (%d) "
			   "encountered in imxScreenInit()\n", type);
		ret = FALSE;
		break;
	}
	if (!ret)
		return FALSE;

	if (pScrn->bitsPerPixel > 8) {
		/* Fixup RGB ordering */
		visual = pScreen->visuals + pScreen->numVisuals;
		while (--visual >= pScreen->visuals) {
			if ((visual->class | DynamicClass) == DirectColor) {
				visual->offsetRed   = pScrn->offset.red;
				visual->offsetGreen = pScrn->offset.green;
				visual->offsetBlue  = pScrn->offset.blue;
				visual->redMask     = pScrn->mask.red;
				visual->greenMask   = pScrn->mask.green;
				visual->blueMask    = pScrn->mask.blue;
			}
		}
	}

	/* must be after RGB ordering fixed */
	if (init_picture && !fbPictureInit(pScreen, NULL, 0))
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			   "Render extension initialisation failed\n");

	xf86SetBlackWhitePixels(pScreen);

	/* INIT ACCELERATION BEFORE INIT FOR BACKING STORE & SOFTWARE CURSOR */ 
  fPtr->useAccel = FALSE;

	/* note if acceleration is in use */
  xf86DrvMsg(pScrn->scrnIndex, X_INFO, "No acceleration in use\n");


	/* Initialize for X extensions. */
	imxExtInit();

	xf86SetBackingStore(pScreen);

	/* software cursor */
	miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

	/* colormap */
	switch ((type = fbdevHWGetType(pScrn)))
	{
	/* XXX It would be simpler to use miCreateDefColormap() in all cases. */
	case FBDEVHW_PACKED_PIXELS:
		if (!miCreateDefColormap(pScreen)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                                   "internal error: miCreateDefColormap failed "
				   "in imxScreenInit()\n");
			return FALSE;
		}
		break;
	case FBDEVHW_INTERLEAVED_PLANES:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: interleaved planes are not yet "
			   "supported by the imx driver\n");
		return FALSE;
	case FBDEVHW_TEXT:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: text mode is not supported by "
			   "the imx driver\n");
		return FALSE;
	case FBDEVHW_VGA_PLANES:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: EGA/VGA planes are not yet "
			   "supported by the imx driver\n");
		return FALSE;
	default:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: unrecognised imx hardware type "
			   "(%d) encountered in imxScreenInit()\n", type);
		return FALSE;
	}
	flags = CMAP_PALETTED_TRUECOLOR;
	if(!xf86HandleColormaps(pScreen, 256, 8, fbdevHWLoadPaletteWeak(), 
				NULL, flags))
		return FALSE;

	xf86DPMSInit(pScreen, fbdevHWDPMSSetWeak(), 0);

	pScreen->SaveScreen = fbdevHWSaveScreenWeak();

	/* Wrap the current CloseScreen function */
	fPtr->saveCloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = imxCloseScreen;

#if IMX_XVIDEO_ENABLE
	{
	    XF86VideoAdaptorPtr *ptr;

	    int n = xf86XVListGenericAdaptors(pScrn,&ptr);
	    if (n) {
		xf86XVScreenInit(pScreen,ptr,n);
	    }
	}
#endif

	if (!imxDisplayFinishScreenInit(pScrn->scrnIndex, pScreen)) {
		return FALSE;
	}

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"initial screen size = %dx%d\n",
		pScrn->virtualX,
		pScrn->virtualY);

	return TRUE;
}

Bool
IMXGetPixmapProperties(
	PixmapPtr pPixmap,
	void** pPhysAddr,
	int* pPitch)
{

  /* not supported when acceleration turned off */
  return FALSE;
}

static Bool
imxProbe(DriverPtr drv, int flags)
{
	/* For now, just bail out for PROBE_DETECT. */
	if (flags & PROBE_DETECT) {
		return FALSE;
	}

	/* Load fbdevhw support */
	if (!xf86LoadDrvSubModule(drv, "fbdevhw")) {
	    return FALSE;
	}

	/* Find all of the device sections in the config */
	GDevPtr *sections;
	int nsects;
	nsects = xf86MatchDevice(IMX_NAME, &sections);
	if (nsects <= 0) {
		return FALSE;
	}

	Bool foundScreen = FALSE;
	int i;
	for (i = 0; i < nsects; i++) {

		/* Get required device name from xorg.conf */
		char* dev = xf86FindOptionValue(sections[i]->options, OPTION_STR_FBDEV);
		if (NULL == dev) {
			xf86Msg(X_WARNING, "Option \"%s\" missing in Section with Identifier \"%s\"\n", OPTION_STR_FBDEV, sections[i]->identifier);
	 		continue;
      		}

		/* Open device */
		int fd = open(dev, O_RDWR, 0);
		if (fd <= 0) {
			xf86Msg(X_WARNING, "Could not open '%s': %s\n",
				dev, strerror(errno));
			continue;
		}

		/* Read fixed screen info from fb device. */
		struct fb_fix_screeninfo info;
		if (ioctl(fd, FBIOGET_FSCREENINFO, &info)) {
			xf86Msg(X_WARNING, "Unable to read hardware info "
					"from %s: %s\n", dev, strerror(errno));
			close(fd);
			continue;
		}
		close(fd);

		/* Make sure that this is a imx driver */
		if (ImxFbTypeUnknown == imxDisplayGetFrameBufferType(&info)) {
			xf86Msg(X_WARNING, "%s is not an imx device: %s\n", dev, info.id);
			continue;
		}

		int entity = xf86ClaimFbSlot(drv, 0, sections[i], TRUE);
		ScrnInfoPtr pScrn =
			xf86ConfigFbEntity(NULL, 0, entity, NULL, NULL, NULL, NULL);

		xf86Msg(X_INFO, "Add screen %p\n", pScrn);

		/* Setup the hooks for the screen. */
		if (pScrn) {
			foundScreen = TRUE;
			
			pScrn->driverVersion = IMX_VERSION_CURRENT;
			pScrn->driverName    = IMX_DRIVER_NAME;
			pScrn->name          = IMX_NAME;
			pScrn->Probe         = imxProbe;
			pScrn->PreInit       = imxPreInit;
			pScrn->ScreenInit    = imxScreenInit;
			pScrn->FreeScreen    = imxFreeScreen;
			pScrn->SwitchMode    = imxDisplaySwitchMode;
			pScrn->AdjustFrame   = imxDisplayAdjustFrame;
			pScrn->EnterVT       = imxDisplayEnterVT;
			pScrn->LeaveVT       = imxDisplayLeaveVT;
                        pScrn->ValidMode     = imxDisplayValidMode;
			
			xf86DrvMsg(pScrn->scrnIndex, X_INFO, "using %s\n", dev);
		}
	}

	free(sections);
	return foundScreen;
}

/* -------------------------------------------------------------------- */

static const OptionInfoRec *
imxAvailableOptions(int chipid, int busid)
{
	return imxOptions;
}

static void
imxIdentify(int flags)
{
	xf86PrintChipsets(IMX_NAME, "Driver for Freescale IMX processors", imxChipsets);
}

_X_EXPORT DriverRec imxDriver = {
	IMX_VERSION_CURRENT,
	IMX_DRIVER_NAME,
	imxIdentify,
	imxProbe,
	imxAvailableOptions,
	NULL,
	0,
	NULL,
};

MODULESETUPPROTO(imxSetup);

static XF86ModuleVersionInfo imxVersRec =
{
	IMX_DRIVER_NAME,
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	IMX_VERSION_MAJOR, IMX_VERSION_MINOR, IMX_VERSION_PATCH,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	NULL,
	{0,0,0,0}
};

_X_EXPORT XF86ModuleData imxModuleData = { &imxVersRec, imxSetup, NULL };

pointer
imxSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	static Bool setupDone = FALSE;

	if (!setupDone) {
		setupDone = TRUE;
		xf86AddDriver(&imxDriver, module, HaveDriverFuncs);
		return (pointer)1;
	} else {
		if (errmaj) *errmaj = LDR_ONCEONLY;
		return NULL;
	}
}

