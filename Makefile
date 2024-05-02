CC = clang
CFLAGS = -Wall -Werror
PKG_CONFIG = pkg-config --cflags --libs gtk+-3.0 poppler-glib cairo gtk-mac-integration
SOURCES = main.c engine/pdf_engine.c
EXECUTABLE = main

all: build

build:
	$(CC) $(CFLAGS) $(shell $(PKG_CONFIG)) $(SOURCES) -o $(EXECUTABLE)
