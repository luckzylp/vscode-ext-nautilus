CC = gcc
CFLAGS = -Wall -shared -fPIC `pkg-config --cflags libnautilus-extension-4 glib-2.0 gio-2.0`
LIBS = `pkg-config --libs libnautilus-extension-4 glib-2.0 gio-2.0`
TARGET = vscode_nautilus_extension.so
INSTALL_DIR = /usr/lib/x86_64-linux-gnu/nautilus/extensions-4/

all: $(TARGET)

$(TARGET): vscode_nautilus_extension.c
	$(CC) $(CFLAGS) vscode_nautilus_extension.c -o $(TARGET) $(LIBS)

install: $(TARGET)
	mkdir -p $(INSTALL_DIR)
	cp $(TARGET) $(INSTALL_DIR)/

clean:
	rm -f $(TARGET)

.PHONY: all install clean
