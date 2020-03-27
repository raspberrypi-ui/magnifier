bin_PROGRAMS = magnifier
magnifier_SOURCES = magnifier.c
magnifier_CFLAGS = $(shell pkg-config --cflags xcomposite xrender atspi-2) -lXext -lX11 -lpthread
magnifier_LDFLAGS = $(shell pkg-config --libs xcomposite xrender atspi-2)
