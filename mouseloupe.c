/*******************************************************************************
*									       *
* mouseloupe.c -- MouseLoupe main program				       *
*									       *
* Copyright (C) 2001-2005 Luciano Silva					       *
*									       *
* This is free software; you can redistribute it and/or modify it under the    *
* terms of the GNU General Public License as published by the Free Software    *
* Foundation; either version 2 of the License, or (at your option) any later   *
* version.See README for details.       		                       *
*                                                                              *
* This software is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or        *
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License        *
* for more details.							       *
* 									       *
* You should have received a copy of the GNU General Public License along with *
* software; if not, write to the Free Software Foundation, Inc., 59 Temple     *
* Place, Suite 330, Boston, MA  02111-1307 USA		                       *
*									       *
* MouseLoupe - Screen Magnifier 					       *
* Jul, 2001								       *
*									       *
* Written by Dr. Prof. Luciano Silva			luciano@inf.ufpr.br    *
*									       *
* Modifications:							       *
*									       *
*    21 Dec 2004 - Fabio Leite Vieira			flv03@inf.ufpr.br      *
*		 - Mauricley Ribas Azevedo 		mra03@inf.ufpr.br      *
*		 - Thiago de Souza Ferreira 		tsf03@inf.ufpr.br      *
*******************************************************************************/


#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/XShm.h>
#include <atspi/atspi.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define SHM

#define CIRCLE		1
#define RECTANGLE	-1

#define WinMask	PointerMotionMask | PointerMotionHintMask | ButtonMotionMask | ButtonPressMask| ButtonReleaseMask

Display *dsp;
int scr;
GC gc, hudgc;

Window topwin, rootwin;
Pixmap srcpixmap, dstpixmap;
Picture src_picture, dst_picture;
XRenderPictureAttributes pict_attr;

int posx, posy;
int shape = CIRCLE;
Bool useFilter = False;
Bool mvEnable = False;
Bool statLoupe = False;

double magstep = 2;		/* magnify factor */

int srcw = 0;			/* source width  (dstw / magstep) */
int srch = 0;			/* source height (dsth / magstep) */
int dstw = 350;			/* destination width (default = 350) */
int dsth = 350;			/* destination height (default = 350) */

/****************************************************************************************
*					init_border
****************************************************************************************/

void init_border ()
{
	XColor col;

	hudgc = XCreateGC (dsp, topwin, 0, NULL);
	XParseColor (dsp, DefaultColormap (dsp, scr), "green", &col);
	XAllocColor (dsp, DefaultColormap (dsp, scr), &col);
	XSetForeground (dsp, hudgc, col.pixel);
	XParseColor (dsp, DefaultColormap (dsp, scr), NULL, &col);
	XAllocColor (dsp, DefaultColormap (dsp, scr), &col);
	XSetBackground (dsp, hudgc, col.pixel);
}

/****************************************************************************************
*					get_image
****************************************************************************************/

void get_image ()
{
	XImage *im;
	XWindowAttributes xatr;
	Window root, nullwd, *children;
	int wx, wy, sx, sy, sw, sh, dx, dy, lx, ly, null;
	unsigned int wh, ww, lw, lh, nwins, wd;

	// get the location of the mouse pointer
	XQueryPointer (dsp, rootwin, &root, &nullwd, &posx, &posy, &null, &null, (unsigned *) &null);

	// clear the loupe
	XSetForeground (dsp, gc, 0);
	XFillRectangle (dsp, srcpixmap, gc, 0, 0, srcw, srch);

	// read the tree of windows
	if (!XQueryTree (dsp, rootwin, &root, &nullwd, &children, &nwins)) return;

	// get the geometry of the loupe
	if (!XGetGeometry (dsp, topwin, &root, &lx, &ly, &lw, &lh, (unsigned *) &null, (unsigned *) &null)) return;

	// loop through all windows from the bottom up, ignoring the top window (which should be the loupe)
	for (wd = 0; wd < nwins - 1; wd++)
	{
		// get the geometry of the window - ignore this window if it is unreadable
		if (!XGetGeometry (dsp, children[wd], &root, &wx, &wy, &ww, &wh, (unsigned *) &null, (unsigned *) &null)) continue;

		// if the geometry of the window matches that of the loupe, ignore it - it's probably the loupe with another window temporarily above it
		if (lx == wx && ly == wy && lw == ww && lh == wh) continue;

		// ignore any windows which have no output, or which are not viewable
		if (!XGetWindowAttributes (dsp, children[wd], &xatr)) continue;
		if (xatr.class != InputOutput || xatr.map_state != IsViewable) continue;

		// calculate source region in this window and destination for it in the loupe
		sx = posx - wx - srcw / 2;
		sy = posy - wy - srch / 2;
		sw = srcw;
		sh = srch;
		dx = 0;
		dy = 0;

		if (sx < 0)
		{
			dx = -sx;
			sw += sx;
			if (sw >= ww) sw = ww;
			sx = 0;
		}
		else if (sx + srcw >= ww) sw = ww - sx;
		if (sw <= 0) continue;

		if (sy < 0)
		{
			dy = -sy;
			sh += sy;
			if (sh >= wh) sh = wh;
			sy = 0;
		}
		else if (sy + srch >= wh) sh = wh - sy;
		if (sh <= 0) continue;

		// copy the source image to the destination pixmap
#ifdef SHM
		XShmSegmentInfo shi;
		im = XShmCreateImage (dsp, DefaultVisual (dsp, scr), DefaultDepth (dsp, scr), ZPixmap, NULL, &shi, sw, sh);
		shi.shmid = shmget (IPC_PRIVATE, (unsigned int) (im->bytes_per_line * im->height), IPC_CREAT | 0777);
		shi.shmaddr = im->data = (char *) shmat (shi.shmid, 0, 0);
		shi.readOnly = False;
		XShmAttach (dsp, &shi);
		XShmGetImage (dsp, children[wd], im, sx, sy, AllPlanes);
		XShmPutImage (dsp, srcpixmap, gc, im, 0, 0, dx, dy, sw, sh, False);
		XShmDetach (dsp, &shi);
		XDestroyImage (im);
		shmdt (shi.shmaddr);
		shmctl (shi.shmid, IPC_RMID, 0);
#else
		im = XGetImage (dsp, children[wd], sx, sy, sw, sh, AllPlanes, ZPixmap);
		if (im != NULL)
		{
			XPutImage (dsp, srcpixmap, gc, im, 0, 0, dx, dy, sw, sh);
			XDestroyImage (im);
		}
#endif

		// composite this window segment over other window segments
		XRenderComposite (dsp, PictOpOver, src_picture, None, dst_picture, 0, 0, 0, 0, 0, 0, dstw, dsth);
	}

	// draw the border
	if (shape == CIRCLE)
		XDrawArc (dsp, dstpixmap, hudgc, 1, 1, dstw - 2, dsth - 2, 0, 360 * 64);
	else
		XDrawRectangle (dsp, dstpixmap, hudgc, 1, 1, dstw - 2, dsth - 2);

	// update the loupe from the composite pixmap
	XCopyArea (dsp, dstpixmap, topwin, gc, 0, 0, dstw, dsth, 0, 0);

	// move the loupe to the top of the stack in case another window has opened
	XRaiseWindow (dsp, topwin);
}

/****************************************************************************************
*					setup_pixmaps
****************************************************************************************/

void setup_pixmaps ()
{
	XTransform t;

	// create the pixmaps and pictures used for scaling
	srcpixmap = XCreatePixmap (dsp, rootwin, srcw, srch, DefaultDepth (dsp, scr));
	dstpixmap = XCreatePixmap (dsp, rootwin, dstw, dsth, DefaultDepth (dsp, scr));
	src_picture = XRenderCreatePicture (dsp, srcpixmap,	XRenderFindStandardFormat (dsp, PictStandardRGB24),	CPRepeat, &pict_attr);
	dst_picture = XRenderCreatePicture (dsp, dstpixmap,	XRenderFindStandardFormat (dsp, PictStandardRGB24),	CPRepeat, &pict_attr);

	// create a scaling matrix (zoom)
	t.matrix[0][0] = XDoubleToFixed (1.0 / magstep);
	t.matrix[0][1] = 0.0;
	t.matrix[0][2] = 0.0;

	t.matrix[1][0] = 0.0;
	t.matrix[1][1] = XDoubleToFixed (1.0 / magstep);
	t.matrix[1][2] = 0.0;
    
	t.matrix[2][0] = 0.0;
	t.matrix[2][1] = 0.0;
	t.matrix[2][2] = XDoubleToFixed (1.0);
	
	// set the transformation matrix
	XRenderSetPictureTransform (dsp, src_picture , &t);

	// set a bilinear filter if requested
	if (useFilter == True) XRenderSetPictureFilter (dsp, src_picture, FilterBilinear, 0, 0);
}

void create_loupe (void)
{
	Pixmap bitmap;
	GC lgc;

	// create a pixmap for the output window
	bitmap = XCreatePixmap (dsp, rootwin, dstw, dsth, 1);
	lgc = XCreateGC (dsp, bitmap, 0, NULL);

	// erase the whole area
	XSetForeground (dsp, lgc, 0);
	XFillRectangle (dsp, bitmap, lgc, 0, 0, dstw, dsth);

	// draw the active area of the loupe
	XSetForeground (dsp, lgc, 1);
	if (shape == CIRCLE) XFillArc (dsp, bitmap, lgc, 0, 0, dstw, dsth, 0, 360 * 64);
	else XFillRectangle (dsp, bitmap, lgc, 0, 0, dstw, dsth);

	// clear an input area so mouse clicks pass through
	XSetForeground (dsp, lgc, 0);
	XFillRectangle (dsp, bitmap, lgc, dstw / 2, dsth / 2, 1, 1);

	// use the resulting pixmap as a mask on the loupe window
	XShapeCombineMask (dsp, topwin, ShapeClip, 0, 0, bitmap, ShapeSet);
	XShapeCombineMask (dsp, topwin, ShapeBounding, 0, 0, bitmap, ShapeSet);

	XFreeGC (dsp, lgc);
	XFreePixmap (dsp, bitmap);
}

/****************************************************************************************
*					SetSize
****************************************************************************************/

void SetSize (int *src, int dst){
float floatres;	// float result
int intres;	// int result

	floatres = dst / magstep;
	intres = (int) floatres;
	if (floatres / intres != 1)
		*src = 1;
	*src += intres;
}


/****************************************************************************************
*					init_screen
****************************************************************************************/
// Initialize all the basic elements of the program, such as: Display, Screen, Window...
void init_screen()
{
	XSetWindowAttributes xset_attr;

	SetSize (&srcw, dstw);
	SetSize (&srch, dsth);
	
	if ((dsp = XOpenDisplay(NULL)) == NULL){
		fprintf (stderr, "Cannot open display conection!\n");
		exit (EXIT_FAILURE);
	}

	scr = DefaultScreen (dsp);
	rootwin = RootWindow (dsp, scr);

	topwin = XCreateSimpleWindow(dsp, rootwin, posx, posy, dstw, dsth,
		5, BlackPixel(dsp, scr), WhitePixel(dsp, scr));

	XSetWindowBorderPixmap(dsp, topwin, CopyFromParent);
	XSetWindowBackgroundPixmap(dsp, topwin, None);

	XSelectInput(dsp, topwin, WinMask);

	// enable the composite extension
	XCompositeRedirectSubwindows (dsp, rootwin, CompositeRedirectAutomatic);
	xset_attr.override_redirect = True;
	XChangeWindowAttributes(dsp, topwin, CWOverrideRedirect, &xset_attr);

	/* setup GC */
	gc = XCreateGC(dsp, rootwin, 0, NULL);
}

/****************************************************************************************
*					strtof
****************************************************************************************/

double _strtof (char *str){
int len;
	len = strlen (str);
	if (strspn (str, "0123456789.") == len)
		return atof(str);
	else
		return (0);
}

/****************************************************************************************
*					strtoi
****************************************************************************************/

int strtoi (char *str){
int len;
	len = strlen (str);
	if (strspn (str, "0123456789") == len)
		return atoi(str);
	else
		return (0);
}

/****************************************************************************************
*					args
****************************************************************************************/
/*	-c [DIAMETER]		circle
	-r [WIDTH] [HEIGHT]	rectangle
	-z MAGNIFICATION	zoom
	-f			filter	
*/

void args (int argc, char **argv){
int i;
char help[] = {"Usage: loupe [OPTIONS]\n\
Opens a screen magnifier under the mouse pointer.\n\n \
\t-c [DIAMETER],\t\tSet a circular shape for the loupe\n \
\t-r [WIDTH] [HEIGHT],\tSet a rectamgular shape for the loupe\n \
\t-z MAG,\t\t\tSet the magnify factor\n \
\t-f,\t\t\tEnable a bilinear filter\n \
\t-m,\t\t\tFollow focus point and text cursor\n \
\t-s,\t\t\tStatic window - drag to move\n \
\t--help,\t\t\tShow this message\n"};

	for (i = 1; i < argc; i++){
		if (argv[i][0] == '-'){
			if (strlen(argv[i]) != 2){
				if (strcmp ("--help", argv[i]) == 0){
					puts (help);
					exit (EXIT_SUCCESS);
				}
				else {
					fprintf (stderr, "%s: invalid option -- %c\n", argv[0], argv[i][1]);
					exit (EXIT_FAILURE);
				}
			}
			switch (argv[i][1]){
				case 'f':
					if (useFilter == False)
						useFilter = True;
					else {
						fprintf (stderr, "%s: duplicated parameter -- %c\n", argv[0], argv[i][1]);
						exit (EXIT_FAILURE);
					}
					break;

				case 'm':
					if (mvEnable == False)
						mvEnable = True;
					else {
						fprintf (stderr, "%s: duplicated parameter -- %c\n", argv[0], argv[i][1]);
						exit (EXIT_FAILURE);
					}
					break;

				case 's':
					if (statLoupe == False)
						statLoupe = True;
					else {
						fprintf (stderr, "%s: duplicated parameter -- %c\n", argv[0], argv[i][1]);
						exit (EXIT_FAILURE);
					}
					break;

				case 'c':
					if (shape == 0){
						shape = CIRCLE;
						if (i+2 <= argc){
							if (argv[i+1][0] != '-'){
								i++;
								if (dstw == 0){
									if (!(dstw = strtoi (argv[i]))){
										fprintf (stderr, "%s: invalid option -- %c\n", argv[0], argv[i][1]);
										exit (EXIT_FAILURE);
									}
									else {
										if ((dstw < 100) || (dstw > 600)){
											fprintf (stderr, "%s: invalid parameter -- %c\nDIAMETER must be between 100 and 600\n", argv[0], argv[i-1][1]);
											exit (EXIT_FAILURE);
										}
									}
									dsth = dstw;
								}
								else {
									fprintf (stderr, "%s: duplicated parameter -- %c\n", argv[0], argv[i][1]);
									exit (EXIT_FAILURE);
								}
							}
						}
					}
					else {
						fprintf (stderr, "%s: shape already set -- %c\n", argv[0], argv[i][1]);
						exit (EXIT_FAILURE);
					}
					break;

				case 'r':
					if (shape == 0){
						shape = RECTANGLE;
						if (i+2 <= argc){
							if (argv[i+1][0] != '-'){
								i++;
								if (dstw == 0){
									if (!(dstw = strtoi (argv[i]))){
										fprintf (stderr, "%s: invalid option -- %c\n", argv[0], argv[i][1]);
										exit (EXIT_FAILURE);
									}
									else {
										if ((dstw < 100) || (dstw > 800)){
											fprintf (stderr, "%s: invalid parameter -- %c\nWIDTH must be between 100 and 800 pixels\n", argv[0], argv[i-1][1]);
											exit (EXIT_FAILURE);
										}
									}
								}
								else {
									fprintf (stderr, "%s: duplicated parameter -- %c\n", argv[0], argv[i][1]);
									exit (EXIT_FAILURE);
								}
								if (i+2 <= argc){
									if (argv[i+1][0] != '-'){
										i++;
										if (dsth == 0){
											if (!(dsth = strtoi (argv[i]))){
												fprintf (stderr, "%s: invalid option -- %c\n", argv[0], argv[i][1]);
												exit (EXIT_FAILURE);
											}
											else {
												if ((dsth < 50) || (dsth > 600)){
													fprintf (stderr, "%s: invalid parameter -- %c\nHEIGHT must be between 50 and 600 pixels\n", argv[0], argv[i-2][1]);
													exit (EXIT_FAILURE);
												}
											}
										}
										else {
											fprintf (stderr, "%s: duplicated parameter -- %c\n", argv[0], argv[i][1]);
											exit (EXIT_FAILURE);
										}
									}
									else {
										if (dsth == 0)
											dsth = dstw;
										else {
											fprintf (stderr, "%s: duplicated parameter -- %c\n", argv[0], argv[i][1]);
											exit (EXIT_FAILURE);
										}
									}
								}
								else {
									if (dsth == 0)
										dsth = dstw;
									else {
										fprintf (stderr, "%s: duplicated parameter -- %c\n", argv[0], argv[i][1]);
										exit (EXIT_FAILURE);
									}
								}
							}
						}
					}
					else {
						fprintf (stderr, "%s: shape already set -- %c\n", argv[0], argv[i][1]);
						exit (EXIT_FAILURE);
					}
					break;
				case 'z':	
					if (magstep == 0){
						if (i+2 <= argc){
							if (argv[i+1][0] != '-'){
								i++;
								if (magstep == 0){
									if (!(magstep = _strtof (argv[i]))){
										fprintf (stderr, "%s: invalid option -- %c\n", argv[0], argv[i-1][1]);
										exit (EXIT_FAILURE);
									}
									else {
										if ((magstep < 2) || (magstep > 16)){
											fprintf (stderr, "%s: invalid parameter -- %c\nMAG must be between 2.0 and 16.0\n", argv[0], argv[i-1][1]);
											exit (EXIT_FAILURE);
										}
									}
								}
								else {
									fprintf (stderr, "%s: duplicated parameter -- %c\n", argv[0], argv[i][1]);
									exit (EXIT_FAILURE);
								}
							}
							else {
								fprintf (stderr, "%s: duplicated parameter -- %c\n", argv[0], argv[i][1]);
								exit (EXIT_FAILURE);
							}
						}
						else {
							fprintf (stderr, "%s: too few arguments -- %c\n",argv[0], argv[i][1]);
							exit (EXIT_FAILURE);
						}
					}
					else {
						fprintf (stderr, "%s: duplicated parameter -- %c\n", argv[0], argv[i][1]);
						exit (EXIT_FAILURE);
					}
					break;
				default:
					fprintf (stderr, "%s: invalid option -- %c\n", argv[0], argv[i][1]);
					exit (EXIT_FAILURE);
			}
		}
		else {
			fprintf (stderr, "%s: invalid option -- %c.\n", argv[0], argv[i][1]);
			exit (EXIT_FAILURE);
		}
	}
}


/****************************************************************************************
*					ErrorHandler
****************************************************************************************/


int ErrorHandler (Display *dpy, XErrorEvent *ev){
	return (0);
}

static void atspi_event (const AtspiEvent *event, void *data)
{
	AtspiRect *rect;
	GError *err;

	if (!mvEnable) return;
	if (event->source == NULL) return;
	if (!g_strcmp0 (event->type, "object:text-caret-moved"))
		rect = atspi_text_get_character_extents ((AtspiText *) event->source, event->detail1, ATSPI_COORD_TYPE_SCREEN, &err);
	else if (!g_strcmp0 (event->type, "object:state-changed:focused"))
		rect = atspi_component_get_extents ((AtspiComponent *) event->source, ATSPI_COORD_TYPE_SCREEN, &err);
	else return;
	if (rect->x <= 0 || rect->y <= 0 || rect->width <= 0 || rect->height <= 0) return;
	XWarpPointer (dsp, None, rootwin, None, None, None, None, rect->x + rect->width / 2, rect->y + rect->height / 2);
}

void *atspi_main (void *param)
{
	atspi_event_main ();
	return NULL;
}

/****************************************************************************************
*					main
****************************************************************************************/


int main (int argc, char **argv)
{
	XEvent ev;
	int drag = 0;
	args (argc, argv);

	XInitThreads ();
	XSetErrorHandler (ErrorHandler);
	init_screen ();
	setup_pixmaps ();
	create_loupe ();
	init_border ();
	get_image ();
	XMapRaised (dsp, topwin);

	if (mvEnable)
	{
		pthread_t atspi_thread;
		atspi_init ();
		AtspiEventListener *listener = atspi_event_listener_new ((AtspiEventListenerCB) atspi_event, NULL, NULL);
		atspi_event_listener_register (listener, "object:text-caret-moved", NULL);
		atspi_event_listener_register (listener, "object:state-changed:focused", NULL);
		pthread_create (&atspi_thread, NULL, atspi_main, NULL);
	}

	while (1)
	{
		get_image ();
		if (statLoupe)
		{
			if (XCheckWindowEvent (dsp, topwin, WinMask, &ev))
			{
				if (ev.type == ButtonPress) drag = 1;
				if (ev.type == ButtonRelease) drag = 0;
				if (ev.type == MotionNotify && drag) XMoveWindow (dsp, topwin, posx - (dstw / 2) - 5, posy - (dsth / 2) - 5);
			}
		}
		else XMoveWindow (dsp, topwin, posx - (dstw / 2) - 5, posy - (dsth / 2) - 5);
	}

	XCloseDisplay (dsp);
	exit (0);
}
