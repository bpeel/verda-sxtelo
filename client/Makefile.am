bin_PROGRAMS = verda-sxtelo-client

INCLUDES = \
	$(GLIB_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(JSON_GLIB_CFLAGS)

source_h = \
	$(srcdir)/vsx-connection.h \
	$(srcdir)/vsx-player.h \
	$(NULL)

verda_sxtelo_client_SOURCES = \
	$(source_h) \
	$(srcdir)/vsx-connection.c \
	$(srcdir)/vsx-enum-types.c \
	$(srcdir)/vsx-main.c \
	$(srcdir)/vsx-marshal.c \
	$(srcdir)/vsx-player.c \
	$(srcdir)/vsx-player-private.h \
	$(NULL)

verda_sxtelo_client_LDFLAGS = \
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
glib_enum_h = vsx-enum-types.h
glib_enum_c = vsx-enum-types.c
glib_enum_headers = $(source_h)
include $(top_srcdir)/build/Makefile.am.enums

# glib-genmarshal rules
glib_marshal_list = vsx-marshal.list
glib_marshal_prefix = vsx_marshal
include $(top_srcdir)/build/Makefile.am.marshal
