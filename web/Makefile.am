TRANSLATABLE_FILES = \
	$(srcdir)/index.html \
	$(srcdir)/verda-sxtelo.js

LANGUAGES = \
	eo \
	en

LANGUAGE_STAMPS = \
	$(LANGUAGES:%=%.language-stamp)

LANGUAGE_FILES = \
	$(LANGUAGES:%=%.language-file)

EXTRA_DIST = \
	$(LANGUAGE_FILES) \
	$(TRANSLATABLE_FILES) \
	translate-file.pl \
	sound.svg \
	$(LANGUAGES:%=%/help.html)

webfilesdir = $(datadir)/web
dist_webfiles_DATA = \
	verda-sxtelo.css \
	jquery-1.9.1.min.js \
	favicon.ico \
	favicon.png \
	message-alert-sound.ogg \
	message-alert-sound.mp3 \
	shout-sound.ogg \
	shout-sound.mp3 \
	turn-sound.ogg \
	turn-sound.mp3 \
	sound-on.png \
	sound-off.png

%.language-stamp : %.language-file $(TRANSLATABLE_FILES) $(srcdir)/translate-file.pl
	lang=`echo "$<" | sed 's/.*\([a-z][a-z]\).language-file/\1/'`; \
	mkdir -p "$(builddir)/$$lang"; \
	for x in $(TRANSLATABLE_FILES); do \
		bn=`basename "$$x"`; \
		perl $(srcdir)/translate-file.pl \
		-l $< -i "$$x" -o "$(builddir)/$$lang/$$bn"; \
	done; \
	touch $@;

all-local : $(LANGUAGE_STAMPS)

CLEANFILES = \
	$(LANGUAGE_STAMPS) \
	$(LANGUAGES:%=%/*.html) \
	$(LANGUAGES:%=%/*.js)

install-data-hook :
	for lang in $(LANGUAGES); do \
		install -m 755 -d "$(DESTDIR)$(webfilesdir)/$$lang"; \
		for tf in $(TRANSLATABLE_FILES); do \
			bn=`basename "$$tf"`; \
			install -m 644 -t "$(DESTDIR)$(webfilesdir)/$$lang" "$(builddir)/$$lang/$$bn"; \
		done; \
		install -m 644 -t "$(DESTDIR)$(webfilesdir)/$$lang" "$(srcdir)/$$lang/help.html"; \
	done

uninstall-hook :
	for lang in $(LANGUAGES); do \
		for tf in $(TRANSLATABLE_FILES); do \
			bn=`basename "$$tf"`; \
			rm -f "$(DESTDIR)$(webfilesdir)/$$lang/$$bn"; \
		done; \
		rm -f "$(DESTDIR)$(webfilesdir)/$$lang/help.html"; \
	done
