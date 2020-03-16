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
#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>

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

Window xxroot_return, xxchild_return, winfocus;
unsigned int xxmask_return;

XWindowAttributes attr1;

double magstep = 0;		/* magnify factor */

int srcw = 0;			/* source width  (dstw / magstep) */
int srch = 0;			/* source height (dsth / magstep) */
int dstw = 0;			/* destination width (default = 350) */
int dsth = 0;			/* destination height (default = 350) */


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
*					move
****************************************************************************************/
// Move the window (topwin) to the current cursor position
void move(){
	XMoveWindow(dsp, topwin, posx-(dstw/2)-5, posy-(dsth/2)-5);
}

/****************************************************************************************
*					grab_cursor
****************************************************************************************/
// Get the cursor coordinates and the current window's ID
void grab_cursor(){

	XQueryPointer(dsp, rootwin, &xxroot_return, &xxchild_return,
		&posx, &posy, &wposx, &wposy, &xxmask_return);

	if (xxchild_return != topwin){
		winfocus = xxchild_return;
		XGetWindowAttributes(dsp, winfocus, &attr1);
	}
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
// This function gets the image from winfocus and copies it to a pixmap then to topwin.
void get_image(){

	XImage *im;
	XWindowAttributes xatr;
	Window root, nullwd, *children;
	int mx, my, wx, wy, sx, sy, sw, sh, dx, dy, null1, null2, null3;
	unsigned int wh, ww, nwins, wd;

	XSetForeground(dsp, gc, 0);
	XFillRectangle(dsp, srcpixmap, gc, 0, 0, srcw, srch);
		
	XQueryTree (dsp, rootwin, &root, &nullwd, &children, &nwins);
	for (wd = 0; wd < nwins - 1; wd++)
	{
		XGetWindowAttributes (dsp, children[wd], &xatr);
		if (xatr.class != InputOutput || xatr.map_state != IsViewable) continue;

		XQueryPointer (dsp, children[wd], &root, &nullwd, &null1, &null2, &mx, &my, (unsigned *) &null3);
		XGetGeometry (dsp, children[wd], &root, &wx, &wy, &ww, &wh, (unsigned *) &null1, (unsigned *) &null2);

		sx = mx - srcw / 2;
		sy = my - srch / 2;
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

		im = XGetImage (dsp, children[wd], sx, sy, sw, sh, AllPlanes, ZPixmap);
		XPutImage (dsp, srcpixmap, gc, im, 0, 0, dx, dy, sw, sh);

		XRenderComposite (dsp, PictOpOver, src_picture, None, dst_picture, 0, 0, 0, 0, 0, 0, dstw, dsth);
	}
	draw_indicator();
	
	XCopyArea(dsp, dstpixmap, topwin, gc, 0, 0, dstw, dsth, 0, 0);
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
// Create the source pixmap and the destiny pixmap, which will be used to get and transform
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

	SetWindowsEvents(DefaultRootWindow(dsp), AllWinMask);
	
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

/****************************************************************************************
*					main
****************************************************************************************/


int main(int argc, char **argv){
XEvent ev;
char text[10];
KeySym key;
Bool 	alt, ctrl,
	other, quit;
int count = 0;

	ctrl = alt = other = quit = False;
	args(argc, argv);
	
	XSetErrorHandler (ErrorHandler);
	
	init_screen();
	
	grab_cursor();	
	winfocus = xxchild_return;	
	get_image();
	move();

	XMapRaised(dsp, topwin);

	while (!quit){
		XNextEvent(dsp, &ev);
		grab_cursor();
		get_image();
		move();
		switch (ev.type){

			case ConfigureNotify:
				XRaiseWindow(dsp, topwin);
				break;

			case MapNotify:
				SetWindowsEvents (ev.xmap.window, AllWinMask);
				//XRaiseWindow(dsp, topwin);
				//XSync (dsp, True);
				break;

			case KeyPress:
				XLookupString(&ev.xkey, text, 10, &key, NULL);
				switch (key){

					case XK_KP_Enter:
					case XK_Return:
						other = True;
						if (mvEnable){
							posy += 15;
							wposy += 15;
							XWarpPointer(dsp, None, rootwin, None, None, None, None, posx, posy);
						}
						break;

					case XK_BackSpace:
						other = True;
						if (mvEnable){
							posx -= 10;
							XWarpPointer(dsp, None, rootwin, None, None, None, None, posx, posy);
						}
						break;

					case XK_Alt_L:
						alt = True;
						break;

					case XK_Control_L:
						ctrl = True;
						break;

					case XK_End:
					case XK_q:
						if ((ctrl && alt) && !other){
							quit = True;
							break;
						}

					default:
						other = True;
						if (mvEnable){
							if (count % 3 == 0){
								posx += 30;
								wposx += 30;
								XWarpPointer(dsp, None, rootwin, None, None, None, None, posx, posy);
							}
							count++;
						}
				};

			case KeyRelease:
				XLookupString(&ev.xkey, text, 10, &key, NULL);
				switch (key){
					case XK_Control_L:
						ctrl = False;
						break;

					case XK_Alt_L:
						alt = False;
						break;

					default:
						other = False;

				};
				break;
		};
	}

	XCloseDisplay(dsp);
	exit(0);
}
