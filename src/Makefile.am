bin_PROGRAMS = gemelo

INCLUDES = \
	$(GLIB_CFLAGS) \
	-DGML_LIBDIR=\""$(libdir)"\"

source_h = \
	$(srcdir)/gml-chunked-iconv.h \
	$(srcdir)/gml-conversation.h \
	$(srcdir)/gml-conversation-set.h \
	$(srcdir)/gml-http-parser.h \
	$(srcdir)/gml-leave-handler.h \
	$(srcdir)/gml-main-context.h \
	$(srcdir)/gml-new-person-handler.h \
	$(srcdir)/gml-new-person-response.h \
	$(srcdir)/gml-parse-content-type.h \
	$(srcdir)/gml-person.h \
	$(srcdir)/gml-person-set.h \
	$(srcdir)/gml-request-handler.h \
	$(srcdir)/gml-response.h \
	$(srcdir)/gml-send-message-handler.h \
	$(srcdir)/gml-server.h \
	$(srcdir)/gml-string-response.h \
	$(srcdir)/gml-watch-person-handler.h \
	$(srcdir)/gml-watch-person-response.h

gemelo_SOURCES = \
	$(source_h) \
	$(srcdir)/gml-chunked-iconv.c \
	$(srcdir)/gml-conversation.c \
	$(srcdir)/gml-conversation-set.c \
	$(srcdir)/gml-http-parser.c \
	$(srcdir)/gml-leave-handler.c \
	$(srcdir)/gml-main.c \
	$(srcdir)/gml-main-context.c \
	$(srcdir)/gml-new-person-handler.c \
	$(srcdir)/gml-new-person-response.c \
	$(srcdir)/gml-parse-content-type.c\
	$(srcdir)/gml-person.c \
	$(srcdir)/gml-person-set.c \
	$(srcdir)/gml-request-handler.c \
	$(srcdir)/gml-response.c \
	$(srcdir)/gml-send-message-handler.c \
	$(srcdir)/gml-server.c \
	$(srcdir)/gml-string-response.c \
	$(srcdir)/gml-watch-person-handler.c \
	$(srcdir)/gml-watch-person-response.c

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
