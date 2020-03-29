bin_PROGRAMS = mag-pi
mag_pi_SOURCES = mag-pi.c
mag_pi_CFLAGS = $(shell pkg-config --cflags xcomposite xrender atspi-2) -lXext -lX11 -lpthread
mag_pi_LDFLAGS = $(shell pkg-config --libs xcomposite xrender atspi-2)
