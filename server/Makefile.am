bin_PROGRAMS = verda-sxtelo

INCLUDES = \
	$(GLIB_CFLAGS) \
	-DVSX_LIBDIR=\""$(libdir)"\"

if USE_SYSTEMD
INCLUDES += $(LIBSYSTEMD_CFLAGS)
endif

source_h = \
	$(srcdir)/vsx-arguments.h \
	$(srcdir)/vsx-chunked-iconv.h \
	$(srcdir)/vsx-conversation.h \
	$(srcdir)/vsx-conversation-set.h \
	$(srcdir)/vsx-flags.h \
	$(srcdir)/vsx-http-parser.h \
	$(srcdir)/vsx-keep-alive-handler.h \
	$(srcdir)/vsx-leave-handler.h \
	$(srcdir)/vsx-list.h \
	$(srcdir)/vsx-log.h \
	$(srcdir)/vsx-main-context.h \
	$(srcdir)/vsx-move-tile-handler.h \
	$(srcdir)/vsx-new-person-handler.h \
	$(srcdir)/vsx-object.h \
	$(srcdir)/vsx-parse-content-type.h \
	$(srcdir)/vsx-person.h \
	$(srcdir)/vsx-person-set.h \
	$(srcdir)/vsx-player.h \
	$(srcdir)/vsx-request-handler.h \
	$(srcdir)/vsx-response.h \
	$(srcdir)/vsx-send-message-handler.h \
	$(srcdir)/vsx-set-n-tiles-handler.h \
	$(srcdir)/vsx-server.h \
	$(srcdir)/vsx-shout-handler.h \
	$(srcdir)/vsx-signal.h \
	$(srcdir)/vsx-simple-handler.h \
	$(srcdir)/vsx-start-typing-handler.h \
	$(srcdir)/vsx-stop-typing-handler.h \
	$(srcdir)/vsx-string-response.h \
	$(srcdir)/vsx-tile.h \
	$(srcdir)/vsx-tile-data.h \
	$(srcdir)/vsx-turn-handler.h \
	$(srcdir)/vsx-watch-person-handler.h \
	$(srcdir)/vsx-watch-person-response.h

verda_sxtelo_SOURCES = \
	$(source_h) \
	$(srcdir)/vsx-arguments.c \
	$(srcdir)/vsx-chunked-iconv.c \
	$(srcdir)/vsx-conversation.c \
	$(srcdir)/vsx-conversation-set.c \
	$(srcdir)/vsx-http-parser.c \
	$(srcdir)/vsx-keep-alive-handler.c \
	$(srcdir)/vsx-leave-handler.c \
	$(srcdir)/vsx-list.c \
	$(srcdir)/vsx-log.c \
	$(srcdir)/vsx-main.c \
	$(srcdir)/vsx-main-context.c \
	$(srcdir)/vsx-move-tile-handler.c \
	$(srcdir)/vsx-new-person-handler.c \
	$(srcdir)/vsx-object.c \
	$(srcdir)/vsx-parse-content-type.c\
	$(srcdir)/vsx-person.c \
	$(srcdir)/vsx-person-set.c \
	$(srcdir)/vsx-player.c \
	$(srcdir)/vsx-request-handler.c \
	$(srcdir)/vsx-response.c \
	$(srcdir)/vsx-send-message-handler.c \
	$(srcdir)/vsx-set-n-tiles-handler.c \
	$(srcdir)/vsx-server.c \
	$(srcdir)/vsx-simple-handler.c \
	$(srcdir)/vsx-shout-handler.c \
	$(srcdir)/vsx-start-typing-handler.c \
	$(srcdir)/vsx-stop-typing-handler.c \
	$(srcdir)/vsx-string-response.c \
	$(srcdir)/vsx-tile-data.c \
	$(srcdir)/vsx-turn-handler.c \
	$(srcdir)/vsx-watch-person-handler.c \
	$(srcdir)/vsx-watch-person-response.c

verda_sxtelo_LDFLAGS = \
	$(GLIB_LIBS)

if USE_SYSTEMD
verda_sxtelo_LDFLAGS += $(LIBSYSTEMD_LIBS)

dist_systemd_DATA = \
	$(builddir)/verda-sxtelo.service \
	$(srcdir)/verda-sxtelo.socket

systemddir = $(sysconfdir)/verda-sxtelo/services
endif

%.service : %.service.in
	$(AM_V_GEN)sed -e 's,@bindir\@,$(bindir),g' $< > $@

EXTRA_DIST = \
	verda-sxtelo.service.in
