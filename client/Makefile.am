bin_PROGRAMS = gemelo-client

INCLUDES = \
	$(GLIB_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(JSON_GLIB_CFLAGS)

source_h = \
	$(srcdir)/gml-connection.h

gemelo_client_SOURCES = \
	$(source_h) \
	$(srcdir)/gml-connection.c \
	$(srcdir)/gml-enum-types.c \
	$(srcdir)/gml-main.c \
	$(srcdir)/gml-marshal.c

gemelo_client_LDFLAGS = \
	$(GLIB_LIBS) \
	$(SOUP_LIBS) \
	$(JSON_GLIB_LIBS) \
	-lreadline \
	-ltermcap

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
