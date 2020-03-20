################################################################################
#									       #
# mouseloupe.c -- MouseLoupe main program	   	 		       #
#									       #
# Copyright (C) 2001-2005 Luciano Silva					       #
#									       #
# This is free software; you can redistribute it and/or modify it under the    #
# terms of the GNU General Public License as published by the Free Software    #
# Foundation; either version 2 of the License, or (at your option) any later   #
# version.See README for details.       		                       #
#                                                                              #
# This software is distributed in the hope that it will be useful, but WITHOUT #
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or        #
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License        #
# for more details.							       #
# 									       #
# You should have received a copy of the GNU General Public License along with #
# software; if not, write to the Free Software Foundation, Inc., 59 Temple     #
# Place, Suite 330, Boston, MA  02111-1307 USA		                       #
#									       #
# MouseLoupe - Screen Magnifier 					       #
# Jul, 2001								       #
#									       #
# Written by Dr. Prof. Luciano Silva			luciano@inf.ufpr.br    #
#									       #
# Modifications:							       #
#									       #
#    21 Dec 2004 - Fabio Leite Vieira			flv03@inf.ufpr.br      #
#		 - Mauricley Ribas Azevedo 		mra03@inf.ufpr.br      #
#		 - Thiago de Souza Ferreira 		tsf03@inf.ufpr.br      #
#									       #
################################################################################


LIBS=`pkg-config --libs xcomposite xfixes xdamage xrender atk-bridge-2.0 atspi-2`
INCLUDES= `pkg-config --cflags xcomposite xfixes xdamage xrender atk-bridge-2.0 atspi-2`
CC = gcc -g -Wunused -Wall -O4 

LDFLAGS =  -L/usr/X11R6/lib -lXext -lX11 -lpthread -lm $(LIBS)

CFLAGS = -o $@ -c $(INCLUDES) -I/usr/X11R6/include -Dlinux -DFUNCPROTO=15 -DNARROWPROTO -fomit-frame-pointer


PROG =	./mouseloupe
OBJ =	./mouseloupe.o
SRC =	./mouseloupe.c

$(PROG): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) $<

install: all
	@./install-sh

all: $(PROG)

clean:
	@echo "Removing temporary files..."
	@rm -f *.~* *.bak core* *.o
	@echo "Done."

distclean:
	@echo "Removing temporary and binary files..."
	@rm -rf *.~* *.bak core* ./mouseloupe ./mouseloupe.o
	@echo "Done."

uninstall:
	@echo "Uninstalling the program..."
	@rm -f /usr/local/bin/mouseloupe /usr/local/bin/mouseloupegui /usr/share/man/man1/mouseloupe.1.gz /usr/man/man1/mouseloupe.1.gz
	@echo "Done"


