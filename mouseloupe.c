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
#include <atspi/atspi.h>

#define MOUSELOUPE_VERSION "0.6"

#define CIRCLE		1
#define RECTANGLE	-1

#define WinMask		ExposureMask |\
			KeyPressMask |\
			KeyReleaseMask |\
			PointerMotionMask |\
			PointerMotionHintMask |\
			ButtonMotionMask|\
			EnterWindowMask |\
			LeaveWindowMask |\
			StructureNotifyMask |\
			SubstructureNotifyMask

#define AllWinMask	KeyPressMask |\
			KeyReleaseMask |\
			PointerMotionMask |\
			PointerMotionHintMask |\
			ButtonMotionMask |\
			StructureNotifyMask |\
			SubstructureNotifyMask

char *str_display = "";
char *str_border = "green";	/* border color */
char *str_hudfg = "green";	/* HUD indicator foreground color */
char *str_hudbg = NULL;		/* HUD indicator background color */


int bw = 5;			/* border width */

Display *dsp;
int scr;

static GC gc, hudgc;

XColor col;

Window topwin, rootwin;
Pixmap srcpixmap = None;
Pixmap dstpixmap = None;

Picture src_picture = None;
Picture dst_picture = None;
XRenderPictureAttributes	pict_attr;
XTransform t;

int posx, posy;
int wposx, wposy;
int draw_done = 0;
int shape = 0;
Bool useFilter = False;
Bool mvEnable = False;

double magstep = 0;		/* magnify factor */

int srcw = 0;			/* source width  (dstw / magstep) */
int srch = 0;			/* source height (dsth / magstep) */
int dstw = 0;			/* destination width (default = 350) */
int dsth = 0;			/* destination height (default = 350) */

static void atspi_event (const AtspiEvent *event, void *data)
{
	AtspiRect *rect;
	GError *err;

	if (!mvEnable) return;
	if (event->source == NULL|| !event->detail1) return;
	rect = atspi_text_get_character_extents ((AtspiText *) event->source, event->detail1, ATSPI_COORD_TYPE_SCREEN, &err);
	XWarpPointer (dsp, None, rootwin, None, None, None, None, rect->x, rect->y);
}

/****************************************************************************************
*				SetWindowsEvents
****************************************************************************************/
// Recusively, this function will select the wanted events for all windows
void SetWindowsEvents(Window root, unsigned long mask){
Window parent, *children;
unsigned int n;
int stat, i;

	stat = XQueryTree(dsp, root, &root, &parent, &children, &n);
	if (!stat || !n)
		return;
	
	XSelectInput(dsp, root, mask);
	
	for(i = 0; i < n; i++){
		XSelectInput(dsp, children[i], mask);
		SetWindowsEvents(children[i], mask);
	}

	XFree((char *)children);
}

/****************************************************************************************
*					draw_indicator
****************************************************************************************/

void draw_indicator(){
	if(!draw_done){
		hudgc = XCreateGC(dsp, topwin, 0, NULL);
		XParseColor(dsp, DefaultColormap(dsp, scr), str_hudfg, &col);
		XAllocColor(dsp, DefaultColormap(dsp, scr), &col);
		XSetForeground(dsp, hudgc, col.pixel);
		XParseColor(dsp, DefaultColormap(dsp, scr), str_hudbg, &col);
		XAllocColor(dsp, DefaultColormap(dsp, scr), &col);
		XSetBackground(dsp, hudgc, col.pixel);
		draw_done = True;
	}
	if (shape == CIRCLE)
		// Set a circular border for the lupe
		XDrawArc(dsp, dstpixmap, hudgc, 1, 1, dstw - 2, dsth - 2, 0, 360 * 64);
	else
		// Set a rectangular border for the lupe
		XDrawRectangle (dsp, dstpixmap, hudgc, 1, 1, dstw-2, dsth-2);	
}

/****************************************************************************************
*					get_image
****************************************************************************************/

void get_image()
{
	XImage *im;
	XWindowAttributes xatr;
	Window root, nullwd, *children;
	int wx, wy, sx, sy, sw, sh, dx, dy, lx, ly, null1, null2;
	unsigned int wh, ww, lw, lh, nwins, wd;

	// get the location of the mouse pointer
	XQueryPointer (dsp, rootwin, &root, &nullwd, &posx, &posy, &wposx, &wposy, (unsigned *) &null1);

	// clear the loupe
	XSetForeground (dsp, gc, 0);
	XFillRectangle (dsp, srcpixmap, gc, 0, 0, srcw, srch);

	// read the tree of windows
	if (!XQueryTree (dsp, rootwin, &root, &nullwd, &children, &nwins)) return;

	// get the geometry of the loupe
	if (!XGetGeometry (dsp, topwin, &root, &lx, &ly, &lw, &lh, (unsigned *) &null1, (unsigned *) &null2)) return;

	// loop through all windows from the bottom up, ignoring the top window (which should be the loupe)
	for (wd = 0; wd < nwins - 1; wd++)
	{
		// get the geometry of the window - ignore this window if it is unreadable
		if (!XGetGeometry (dsp, children[wd], &root, &wx, &wy, &ww, &wh, (unsigned *) &null1, (unsigned *) &null2)) continue;

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
		im = XGetImage (dsp, children[wd], sx, sy, sw, sh, AllPlanes, ZPixmap);
		XPutImage (dsp, srcpixmap, gc, im, 0, 0, dx, dy, sw, sh);

		// composite this window segment over other window segments
		XRenderComposite (dsp, PictOpOver, src_picture, None, dst_picture, 0, 0, 0, 0, 0, 0, dstw, dsth);
	}
	draw_indicator ();

	// update the loupe from the composite pixmap
	XCopyArea (dsp, dstpixmap, topwin, gc, 0, 0, dstw, dsth, 0, 0);

	// move the loupe so it is centred on the pointer location
	XMoveWindow (dsp, topwin, posx - (dstw / 2) - 5, posy - (dsth / 2) - 5);
}

/****************************************************************************************
*					set_shape
****************************************************************************************/
// Depending on "shape", sets the loupe format to circular or rectangular
void set_shape(){
Pixmap bitmap;
GC gc;

	bitmap = XCreatePixmap(dsp, rootwin, dstw, dsth, 1);
	gc = XCreateGC(dsp, bitmap, 0, NULL);
	XSetForeground(dsp, gc, 0);
	XFillRectangle(dsp, bitmap, gc, 0, 0, dstw, dsth);

	XSetForeground(dsp, gc, 1);
	
	if (shape == CIRCLE)
		// The loupe will be circle shaped
		XFillArc(dsp, bitmap, gc, 0, 0, dstw, dsth, 0, 360 * 64);
	else
		// rectangular shape...
		XFillRectangle(dsp, bitmap, gc, 0, 0, dstw, dsth);
		
	// set a central hole on loupe window
	XSetForeground(dsp, gc, 0); 
	XFillRectangle(dsp, bitmap, gc, dstw / 2, dsth / 2, 1, 1);

	XShapeCombineMask(dsp, topwin, ShapeClip, 0, 0, bitmap, ShapeSet);
	XShapeCombineMask(dsp, topwin, ShapeBounding, 0, 0, bitmap, ShapeSet);

	XFreeGC(dsp, gc);
	XFreePixmap(dsp, bitmap);

}

/****************************************************************************************
*					resize
****************************************************************************************/
// Create the source pixmap and the destination pixmap, which will be used to get and transform
// the windows contents
void resize(){

	srcpixmap = XCreatePixmap(dsp, rootwin, srcw, srch, DefaultDepth(dsp, scr));
	dstpixmap = XCreatePixmap(dsp, rootwin, dstw, dsth, DefaultDepth(dsp, scr));
	set_shape();
}

/****************************************************************************************
*					SetRender
****************************************************************************************/

void SetRender (void){
int event_base, error_base;

	// Check if the composite extension is present
	if (!XCompositeQueryExtension (dsp, &event_base, &error_base)){
		fprintf (stderr, "No composite extension.\n");
		exit (EXIT_FAILURE);
	}

	// enable the composite extension
	XCompositeRedirectSubwindows (dsp,rootwin, CompositeRedirectAutomatic);
	// Set a scale matrix (zoom)
	t.matrix[0][0] = XDoubleToFixed (1.0/magstep);
	t.matrix[0][1] = 0.0;
	t.matrix[0][2] = 0.0;

	t.matrix[1][0] = 0.0;
	t.matrix[1][1] = XDoubleToFixed (1.0/magstep);
	t.matrix[1][2] = 0.0;
    
	t.matrix[2][0] = 0.0;
	t.matrix[2][1] = 0.0;
	t.matrix[2][2] = XDoubleToFixed (1.0);
	
	src_picture = XRenderCreatePicture (dsp, srcpixmap,
			XRenderFindStandardFormat (dsp, PictStandardRGB24),
			CPRepeat, &pict_attr);

	dst_picture = XRenderCreatePicture (dsp, dstpixmap,
			XRenderFindStandardFormat (dsp, PictStandardRGB24),
			CPRepeat, &pict_attr);
	
	// Set the transformation matrix to the picture
	XRenderSetPictureTransform (dsp, src_picture , &t);

	if (useFilter == True)
		// set a bilinear filter for the picture
		XRenderSetPictureFilter (dsp, src_picture, FilterBilinear, 0, 0);
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
void init_screen(){
XSetWindowAttributes xset_attr;

	SetSize (&srcw, dstw);
	SetSize (&srch, dsth);
	
	if ((dsp = XOpenDisplay(str_display)) == NULL){
		fprintf (stderr, "Cannot open display conection!\n");
		exit (EXIT_FAILURE);
	}

	scr = DefaultScreen(dsp);
	rootwin = RootWindow (dsp, scr);

	topwin = XCreateSimpleWindow(dsp, rootwin, posx, posy, dstw, dsth,
		bw, BlackPixel(dsp, scr), WhitePixel(dsp, scr));

	XSetWindowBorderPixmap(dsp, topwin, CopyFromParent);
	XSetWindowBackgroundPixmap(dsp, topwin, None);

	XSelectInput(dsp, topwin, WinMask);

	xset_attr.override_redirect = True;
	XChangeWindowAttributes(dsp, topwin, CWOverrideRedirect, &xset_attr);

	/* setup GC */
	gc = XCreateGC(dsp, rootwin, 0, NULL);
	
	resize();

	SetRender();
	
}

/****************************************************************************************
*					SetDefault
****************************************************************************************/

void SetDefault (void){

	if (!shape)
		shape = CIRCLE;
	if (!magstep)
		magstep = 2;
	if (!dstw) {
		dstw = 350;
		dsth = 350;
	}
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
	SetDefault();
}


/****************************************************************************************
*					ErrorHandler
****************************************************************************************/


int ErrorHandler (Display *dpy, XErrorEvent *ev){
	return (0);
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
	Bool quit = False;

	args (argc, argv);

	XInitThreads ();
	XSetErrorHandler (ErrorHandler);
	init_screen ();
	get_image ();
	XMapRaised (dsp, topwin);

	if (mvEnable)
	{
		pthread_t atspi_thread;
		atspi_init ();
		AtspiEventListener *listener = atspi_event_listener_new ((AtspiEventListenerCB) atspi_event, NULL, NULL);
		atspi_event_listener_register (listener, "object:text-caret-moved", NULL);
		pthread_create (&atspi_thread, NULL, atspi_main, NULL);
	}

	while (!quit){
		XNextEvent(dsp, &ev);
		get_image();
		switch (ev.type){

			case ConfigureNotify:
				XRaiseWindow (dsp, topwin);
				break;

		};
	}

	XCloseDisplay(dsp);
	exit(0);
}
