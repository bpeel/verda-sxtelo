bin_PROGRAMS = gemelo

INCLUDES = \
	$(GLIB_CFLAGS) \
	-DGML_LIBDIR=\""$(libdir)"\"

source_h = \
	$(srcdir)/gml-http-parser.h \
	$(srcdir)/gml-main-context.h \
	$(srcdir)/gml-server.h

gemelo_SOURCES = \
	$(source_h) \
	$(srcdir)/gml-http-parser.c \
	$(srcdir)/gml-main.c \
	$(srcdir)/gml-main-context.c \
	$(srcdir)/gml-server.c

gemelo_LDFLAGS = \
	$(GLIB_LIBS)

# These need to be defined before we can include the glib Makefiles
# below
CLEANFILES =
DISTCLEANFILES =
BUILT_SOURCES =
EXTRA_DIST =

# glib-mkenums rules
glib_enum_h = gml-enum-types.h
glib_enum_c = gml-enum-types.c
glib_enum_headers = $(source_h)
include $(top_srcdir)/build/Makefile.am.enums

# glib-genmarshal rules
glib_marshal_list = gml-marshal.list
glib_marshal_prefix = gml_marshal
include $(top_srcdir)/build/Makefile.am.marshal
