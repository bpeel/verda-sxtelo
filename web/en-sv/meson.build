foreach file : translatable_files
  custom_target(lang + '-' + file,
                input : [ '../translate-file.py',
                          '..' / 'en.language-file',
                          '..' / lang + '.language-file',
                          '..' / file ],
                output : file,
                command : [ python, '@INPUT0@',
                            '-l', '@INPUT1@',
                            '-l', '@INPUT2@',
                            '-i', '@INPUT3@',
                            '-o', '@OUTPUT0@' ],
                install : true,
                install_dir : web_dir / lang)
endforeach
