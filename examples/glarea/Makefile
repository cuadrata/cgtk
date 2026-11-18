
CC = gcc

# GtkGLArea is built with GTK+ when EGL is found (see ../../configure.ac),
# and is not one of the examples ../Makefile builds, since not every
# system has GL. The library GL itself is in is the program's to
# link: OpenGL where the system has it, else GL.
GL_LIBS = `pkg-config --libs opengl 2>/dev/null || pkg-config --libs gl`

all: glarea glcore glloop

glarea: glarea.c
	$(CC) `gtk-config --cflags` glarea.c -o glarea `gtk-config --libs gtkgl` $(GL_LIBS) -lm

glcore: glcore.c
	$(CC) `gtk-config --cflags` glcore.c -o glcore `gtk-config --libs gtkgl` $(GL_LIBS) -lm

glloop: glloop.c
	$(CC) `gtk-config --cflags` glloop.c -o glloop `gtk-config --libs gtkgl` $(GL_LIBS) -lm

clean:
	rm -f *.o glarea glcore glloop
