bin_PROGRAMS = mage
mage_SOURCES = mage.c
mage_CFLAGS = $(shell pkg-config --cflags xcomposite xrender atspi-2) -lXext -lX11 -lpthread
mage_LDFLAGS = $(shell pkg-config --libs xcomposite xrender atspi-2)
