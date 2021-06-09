/*
Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
All rights reserved.

Based on MouseLoupe, copyright (c) 2001-2005 Luciano Silva
    Fabio Leite Vieira, Mauricley Ribas Azevedo, Thiago de Souza Ferreira

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/XShm.h>
#include <atspi/atspi.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define SHM

#define CIRCLE      0
#define RECTANGLE   1

#define EVENT_MASK  PointerMotionMask | PointerMotionHintMask | ButtonMotionMask | ButtonPressMask| ButtonReleaseMask

Display *dsp;
int scr;
GC gc, hudgc;

Window topwin, rootwin, fwin;
Pixmap srcpixmap, dstpixmap;
Picture src_picture, dst_picture;
XRenderPictureAttributes pict_attr;
XErrorHandler default_handler;

int screenw, screenh;   /* screen size */
int posx, posy;         /* mouse location */
int srcw, srch;         /* source pixmap dimensions */

int shape = RECTANGLE;  /* loupe shape */
int dstw = 350;         /* loupe width */
int dsth = 350;         /* loupe height */
int magstep = 2;        /* magnification factor */
int offset = 0;         /* offset of centre point */

Bool useFilter = False;
Bool mvEnable = False;
Bool fcEnable = False;
Bool statLoupe = False;

Bool ignore_errors = False;
Bool allowErrors = False;

Atom wt_desktop;
Atom wt_dock;
Atom wt_toolbar;
Atom wt_menu;
Atom wt_utility;
Atom wt_splash;
Atom wt_dialog;
Atom wt_dropdown_menu;
Atom wt_popup_menu;
Atom wt_tooltip;
Atom wt_notification;
Atom wt_combo;
Atom wt_dnd;
Atom wt_normal;

/* error handler - allows errors to be masked; otherwise calls X default handler */

int error_handler (Display *dpy, XErrorEvent *ev)
{
    if (!allowErrors) return 0;
    if (!ignore_errors) default_handler (dpy, ev);
    return 0;
}

void init_atoms (void)
{
    wt_desktop = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_DESKTOP", True);
    wt_dock = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_DOCK", True);
    wt_toolbar = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_TOOLBAR", True);
    wt_menu = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_MENU", True);
    wt_utility = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_UTILITY", True);
    wt_splash = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_SPLASH", True);
    wt_dialog = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_DIALOG", True);
    wt_dropdown_menu = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", True);
    wt_popup_menu = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_POPUP_MENU", True);
    wt_tooltip = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_TOOLTIP", True);
    wt_notification = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_NOTIFICATION", True);
    wt_combo = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_COMBO", True);
    wt_dnd = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_DND", True);
    wt_normal = XInternAtom (dsp, "_NET_WM_WINDOW_TYPE_NORMAL", True);
}

Atom get_wd_type (Display *dsp, Window wd)
{
    Window chroot, *ch, nullwd;
    Atom type;
    int ret_fmt, nch;
    unsigned long items, left;
    unsigned char *data;

    if (XGetWindowProperty (dsp, wd, XInternAtom (dsp, "_NET_WM_WINDOW_TYPE", FALSE), 0L, 0x7FFFFFFF, False, XA_ATOM, &type, &ret_fmt, &items, &left, &data) == Success
        && items)
    {
        type = *((Atom *) data);
    }
    else
    {
        XQueryTree (dsp, wd, &chroot, &nullwd, &ch, &nch);
        if (nch && XGetWindowProperty (dsp, ch[0], XInternAtom (dsp, "_NET_WM_WINDOW_TYPE", FALSE), 0L, 0x7FFFFFFF, False, XA_ATOM, &type, &ret_fmt, &items, &left, &data) == Success
            && items)
        {
            type = *((Atom *) data);
        }
        else type = 0;
        XFree (ch);
    }
    return type;
}

/* get_image - construct the image to go in the loupe by copying from each window in turn */

#define CONSTRAIN(lo,hi,max,offset)	 if (lo < 0) { offset += -lo; lo = 0; } if (hi > max) hi = max;
#define CONSTRAIN_BORDER(lo,hi,max,offset,sbord,fbord)	 if (lo < sbord) { offset += sbord - lo; lo = sbord; } if (hi > max - fbord) hi = max - fbord;

void get_image (void)
{
    XImage *im;
    XWindowAttributes xatr;
    Window root, nullwd, *children;
    int sx, sy, sw, sh, dx, dy, null;
    unsigned int nwins, wd;

    // get the location of the mouse pointer
    XQueryPointer (dsp, rootwin, &root, &nullwd, &posx, &posy, &null, &null, (unsigned *) &null);

    // move the loupe to the top of the stack in case another window has opened on top of it
    XRaiseWindow (dsp, topwin);

    // clear the loupe
    XSetForeground (dsp, gc, 0);
    XFillRectangle (dsp, srcpixmap, gc, 0, 0, srcw, srch);

    // read the tree of windows
    if (!XQueryTree (dsp, rootwin, &root, &nullwd, &children, &nwins)) return;

    // loop through all windows from the bottom up, ignoring the top window (which should be the loupe)
    for (wd = 0; wd < nwins - 1; wd++)
    {
        // ignore any windows which have no output, or which are not viewable
        ignore_errors = True;
        if (!XGetWindowAttributes (dsp, children[wd], &xatr)) continue;
        ignore_errors = False;
        if (xatr.class != InputOutput || xatr.map_state != IsViewable) continue;

        // start with destination point in loupe at origin
        dx = 0;
        dy = 0;

        // calculate loupe bounds in screen coords
        sx = posx - srcw / 2;
        sy = posy - srch / 2;
        sw = sx + srcw;
        sh = sy + srch;

        // constrain loupe to screen, moving destination if needed
        CONSTRAIN (sx, sw, screenw, dx);
        CONSTRAIN (sy, sh, screenh, dy);

        // convert source to coords relative to window
        sx -= xatr.x;
        sy -= xatr.y;
        sw -= xatr.x;
        sh -= xatr.y;

        // constrain loupe to window, moving destination if needed
        if (offset)
        {
            Atom type = get_wd_type (dsp, children[wd]);
            if (xatr.override_redirect == True || type == wt_normal)
            {
                CONSTRAIN_BORDER (sx, sw, xatr.width, dx, 10, 10);
                CONSTRAIN_BORDER (sy, sh, xatr.height, dy, 8, 10);
            }
            else if (type == wt_dialog)
            {
                CONSTRAIN_BORDER (sx, sw, xatr.width, dx, 1, 1);
                CONSTRAIN_BORDER (sy, sh, xatr.height, dy, 1, 1);
            }
            else
            {
                CONSTRAIN (sx, sw, xatr.width, dx);
                CONSTRAIN (sy, sh, xatr.height, dy);
            }
        }
        else
        {
            CONSTRAIN (sx, sw, xatr.width, dx);
            CONSTRAIN (sy, sh, xatr.height, dy);
        }

        // convert loupe bounds to width and height
        sw -= sx;
        sh -= sy;
        if (sw <= 0 || sh <= 0) continue;

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
    if (shape == CIRCLE) XDrawArc (dsp, dstpixmap, hudgc, 1, 1, dstw - 2, dsth - 2, 0, 360 * 64);
    else XDrawRectangle (dsp, dstpixmap, hudgc, 1, 1, dstw - 2, dsth - 2);

    // update the loupe from the composite pixmap
    XCopyArea (dsp, dstpixmap, topwin, gc, 0, 0, dstw, dsth, 0, 0);

    XFree (children);
}


/* setup_pixmaps - create the linked source and destination pixmaps used by the loupe */

void setup_pixmaps (void)
{
    XTransform t;

    // calculate source dimensions
    srcw = dstw / magstep + ((dstw % magstep) ? 1 : 0);
    srch = dsth / magstep + ((dsth % magstep) ? 1 : 0);

    // create the pixmaps and pictures used for scaling
    srcpixmap = XCreatePixmap (dsp, rootwin, srcw, srch, DefaultDepth (dsp, scr));
    dstpixmap = XCreatePixmap (dsp, rootwin, dstw, dsth, DefaultDepth (dsp, scr));
    src_picture = XRenderCreatePicture (dsp, srcpixmap, XRenderFindStandardFormat (dsp, PictStandardRGB24), CPRepeat, &pict_attr);
    dst_picture = XRenderCreatePicture (dsp, dstpixmap, XRenderFindStandardFormat (dsp, PictStandardRGB24), CPRepeat, &pict_attr);

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
    XRenderSetPictureTransform (dsp, src_picture, &t);

    // set a bilinear filter if requested
    if (useFilter == True) XRenderSetPictureFilter (dsp, src_picture, FilterBilinear, 0, 0);
}


/* setup_loupe - configure the loupe window itself */

void setup_loupe (void)
{
    Pixmap bitmap;
    XColor col;

    // set the background and border
    XSetWindowBorderPixmap (dsp, topwin, CopyFromParent);
    XSetWindowBackgroundPixmap (dsp, topwin, None);

    // create a pixmap for the output window
    bitmap = XCreatePixmap (dsp, rootwin, dstw, dsth, 1);
    hudgc = XCreateGC (dsp, bitmap, 0, NULL);

    // erase the whole area
    XSetForeground (dsp, hudgc, 0);
    XFillRectangle (dsp, bitmap, hudgc, 0, 0, dstw, dsth);

    // draw the active area of the loupe
    XSetForeground (dsp, hudgc, 1);
    if (shape == CIRCLE) XFillArc (dsp, bitmap, hudgc, 0, 0, dstw, dsth, 0, 360 * 64);
    else XFillRectangle (dsp, bitmap, hudgc, 0, 0, dstw, dsth);

    // clear an input area so mouse clicks pass through
    XSetForeground (dsp, hudgc, 0);
    XFillRectangle (dsp, bitmap, hudgc, dstw / 2 + offset, dsth / 2 + offset, 1, 1);

    // use the resulting pixmap as a mask on the loupe window
    XShapeCombineMask (dsp, topwin, ShapeClip, 0, 0, bitmap, ShapeSet);
    XShapeCombineMask (dsp, topwin, ShapeBounding, 0, 0, bitmap, ShapeSet);

    XFreeGC (dsp, hudgc);
    XFreePixmap (dsp, bitmap);

    // draw the border
    hudgc = XCreateGC (dsp, topwin, 0, NULL);
    XParseColor (dsp, DefaultColormap (dsp, scr), "yellow", &col);
    XAllocColor (dsp, DefaultColormap (dsp, scr), &col);
    XSetForeground (dsp, hudgc, col.pixel);

    // draw the background
    XParseColor (dsp, DefaultColormap (dsp, scr), NULL, &col);
    XAllocColor (dsp, DefaultColormap (dsp, scr), &col);
    XSetBackground (dsp, hudgc, col.pixel);

    XMapRaised (dsp, topwin);
}


/* init_screen - generic X initialisation */

void init_screen (void)
{
    XSetWindowAttributes xset_attr;
    int event_base, error_base;

    dsp = XOpenDisplay (NULL);
    if (dsp == NULL)
    {
        fprintf (stderr, "Cannot open display conection\n");
        exit (EXIT_FAILURE);
    }

    scr = DefaultScreen (dsp);
    rootwin = RootWindow (dsp, scr);
    gc = XCreateGC (dsp, rootwin, 0, NULL);
    screenw = WidthOfScreen (DefaultScreenOfDisplay (dsp));
    screenh = HeightOfScreen (DefaultScreenOfDisplay (dsp));

    // make sure a static loupe will be onscreen
    if (posx >= screenw - dstw - 10) posx = screenw - dstw - 10;
    if (posy >= screenh - dsth - 10) posy = screenh - dsth - 10;

    // create the window which will be used for the loupe
    topwin = XCreateSimpleWindow (dsp, rootwin, posx, posy, dstw, dsth, 5, BlackPixel (dsp, scr), WhitePixel (dsp, scr));
    XSelectInput (dsp, topwin, EVENT_MASK);

    if (!XCompositeQueryExtension (dsp, &event_base, &error_base))
    {
        fprintf (stderr, "No composite extension\n");
        exit (EXIT_FAILURE);
    }

    // enable the composite extension
    XCompositeRedirectSubwindows (dsp, rootwin, CompositeRedirectAutomatic);
    xset_attr.override_redirect = True;
    XChangeWindowAttributes (dsp, topwin, CWOverrideRedirect, &xset_attr);
}


/* intarg - argument parsing helper function - reads integer from string and checks range */

int intarg (char *str, int low, int high)
{
    int val;
    if (sscanf (str, "%d", &val) != 1) return -1;
    if (high == -1) return val;
    if (val < low || val > high) return -1;
    return val;
}


/* args - parse command-line arguments to program */

#define GETINT(l,h) if (argc < i + 2 || argv[i + 1][0] == '-') continue; i++; val = intarg (argv[i], l, h); if (val == -1) goto argerr;

void args (int argc, char **argv)
{
    int i, val;

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            if (strlen(argv[i]) != 2)
            {
                if (strcmp ("--help", argv[i]) == 0)
                {
                    puts (  "Usage: mage [OPTIONS]\n"
                            "Opens a screen magnifier under the mouse pointer.\n\n"
                            "\t-c [DIAMETER]\t\tSet a circular shape for the loupe\n"
                            "\t-r [WIDTH] [HEIGHT]\tSet a rectamgular shape for the loupe\n"
                            "\t-s [X] [Y]\t\tStatic window - drag to move\n"
                            "\t-z MAG\t\t\tSet the magnification factor\n"
                            "\t-o OFFSET\t\tSet the offset of the pointer from the centre of the loupe\n"
                            "\t-f\t\t\tEnable a bilinear filter\n"
                            "\t-m\t\t\tFollow focus point\n"
                            "\t-t\t\t\tFollow text cursor\n"
                            "\t--help\t\t\tShow this message\n" );
                    exit (EXIT_SUCCESS);
                }
                else goto argerr;
            }

            switch (argv[i][1])
            {
                case 'f':   useFilter = True;
                            break;

                case 'm':   fcEnable = True;
                            break;

                case 't':   mvEnable = True;
                            break;

                case 's':   statLoupe = True;
                            GETINT (0, -1);
                            posx = val;
                            GETINT (0, -1);
                            posy = val;
                            break;

                case 'z':   GETINT (2, 16);
                            magstep = val;
                            break;

                case 'c':   shape = CIRCLE;
                            GETINT (100, 600);
                            dstw = dsth = val;
                            break;

                case 'r':   shape = RECTANGLE;
                            GETINT (100, 800);
                            dstw = dsth = val;
                            GETINT (50, 600);
                            dsth = val;
                            break;

                case 'o':   GETINT (0, 20);
                            offset = val;
                            break;

                case 'e':   allowErrors = True;
                            break;

                default:    goto argerr;
            }
        }
        else goto argerr;
    }
    return;

argerr:
    fprintf (stderr, "Invalid option : %s\n", argv[i]);
    exit (EXIT_FAILURE);
}


/* atspi_event - callback on assistive tech event for keyboard or focus move */

static void atspi_event (const AtspiEvent *event, void *data)
{
    AtspiRect *rect;
    GError *err;
    static int drag;

    if (event->source == NULL) return;

    if (!g_strcmp0 (event->type, "mouse:button:1p")) drag = 1;
    if (!g_strcmp0 (event->type, "mouse:button:1r")) drag = 0;

    if (mvEnable && !g_strcmp0 (event->type, "object:text-caret-moved"))
    {
        // any caret move triggers this, including in non-focused windows - can't find a way to stop this...
        rect = atspi_text_get_character_extents ((AtspiText *) event->source, event->detail1, ATSPI_COORD_TYPE_SCREEN, &err);
    }
    else if (fcEnable && !g_strcmp0 (event->type, "object:state-changed:focused") && event->detail1)
    {
        // don't move mouse on focussed window changes; just inside the same window
        Window nfwin;
        int revert;
        XGetInputFocus (dsp, &nfwin, &revert);
        if (fwin != nfwin)
        {
            fwin = nfwin;
            return;
        }
        rect = atspi_component_get_extents ((AtspiComponent *) event->source, ATSPI_COORD_TYPE_SCREEN, &err);
    }
    else return;
    if (rect->x <= 0 || rect->y <= 0 || rect->width <= 0 || rect->height <= 0) return;
    if (!drag) XWarpPointer (dsp, None, rootwin, None, None, None, None, rect->x + rect->width / 2, rect->y + rect->height / 2);
}


/* atspi_main - AT-SPI event processing thread */

void *atspi_main (void *param)
{
    atspi_event_main ();
    return NULL;
}


/* main */

int main (int argc, char *argv[])
{
    XEvent ev;
    int drag = 0;

    args (argc, argv);

    XInitThreads ();
    default_handler = XSetErrorHandler (error_handler);

    init_screen ();
    init_atoms ();
    setup_pixmaps ();
    setup_loupe ();

    if (mvEnable || fcEnable)
    {
        pthread_t atspi_thread;
        atspi_init ();
        AtspiEventListener *listener = atspi_event_listener_new ((AtspiEventListenerCB) atspi_event, NULL, NULL);
        if (mvEnable)
        {
			atspi_event_listener_register (listener, "object:text-caret-moved", NULL);
			atspi_event_listener_register (listener, "mouse:button:1p", NULL);
			atspi_event_listener_register (listener, "mouse:button:1r", NULL);
        }
        if (fcEnable) atspi_event_listener_register (listener, "object:state-changed:focused", NULL);
        pthread_create (&atspi_thread, NULL, atspi_main, NULL);
    }

    while (1)
    {
        get_image ();
        if (statLoupe)
        {
            if (XCheckWindowEvent (dsp, topwin, EVENT_MASK, &ev))
            {
                if (ev.type == ButtonPress) drag = 1;
                if (ev.type == ButtonRelease)
                {
                    drag = 0;
                    system ("lxpanelctl command magnifier pos");
                }
                if (ev.type == MotionNotify && drag) XMoveWindow (dsp, topwin, posx - (dstw / 2) - 5, posy - (dsth / 2) - 5);
            }
        }
        else XMoveWindow (dsp, topwin, posx - (dstw / 2) - 5, posy - (dsth / 2) - 5);
    }

    XCloseDisplay (dsp);
    exit (EXIT_SUCCESS);
}

/* End of file */
