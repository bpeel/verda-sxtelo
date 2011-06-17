TRANSLATABLE_FILES = \
	$(srcdir)/chat.html \
	$(srcdir)/chat.js \
	$(srcdir)/index.html

LANGUAGES = \
	en \
	eo \
	fr

LANGUAGE_STAMPS = \
	$(LANGUAGES:%=%.language-stamp)

LANGUAGE_FILES = \
	$(LANGUAGES:%=%.language-file)

EXTRA_DIST = \
	$(LANGUAGE_FILES) \
	$(TRANSLATABLE_FILES) \
	translate-file.pl

webfilesdir = $(datadir)/web
dist_webfiles_DATA = \
	gemelo.css \
	gemelo-logo.png \
	gemelo-logo.svg \
	jquery-1.6.1.js

webfiles_DATA = \
	index.html.var

%.language-stamp : %.language-file $(TRANSLATABLE_FILES) $(srcdir)/translate-file.pl
	lang=`echo "$<" | sed 's/.*\([a-z][a-z]\).language-file/\1/'`; \
	for x in $(TRANSLATABLE_FILES); do \
		bn=`basename "$$x"`; \
		output=`echo "$$bn" | sed "s/\\(.*\\)\\.\\(.*\\)/\\1.$$lang.\\2/"`; \
		perl $(srcdir)/translate-file.pl \
		-l $< -i "$$x" -o "$(builddir)/$$output"; \
	done; \
	touch $@;

all-local : $(LANGUAGE_STAMPS)

CLEANFILES = \
	$(LANGUAGE_STAMPS) \
	$(LANGUAGES:%=*.%.html) \
	$(LANGUAGES:%=*.%.js) \
	index.html.var

install-data-hook :
	for lang in $(LANGUAGES); do \
		for tf in $(TRANSLATABLE_FILES); do \
			output=`echo "$$tf" | sed "s/\\(.*\\)\\.\\(.*\\)/\\1.$$lang.\\2/"`; \
			install -m 644 -t "$(DESTDIR)$(webfilesdir)" $(builddir)/`basename "$$output"`; \
		done; \
	done

uninstall-hook :
	for lang in $(LANGUAGES); do \
		for tf in $(TRANSLATABLE_FILES); do \
			output=`echo "$$tf" | sed "s/\\(.*\\)\\.\\(.*\\)/\\1.$$lang.\\2/"`; \
			rm -f $(DESTDIR)$(webfilesdir)/`basename "$$output"`; \
		done; \
	done

index.html.var :
	echo "URI: index.en.html" >> $@; \
	echo >> $@; \
	for lang in $(LANGUAGES); do \
		echo "URI: index.$$lang.html" >> $@; \
		echo "Content-Type: text/html; charset=UTF-8" >> $@; \
		echo "Content-Language: $$lang" >> $@; \
		echo >> $@; \
	done;
