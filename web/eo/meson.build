foreach file : translatable_files
  custom_target(lang + '-' + file,
                input : [ '../translate-file.py',
                          '..' / lang + '.language-file',
                          '..' / file ],
                output : file,
                command : [ python, '@INPUT0@',
                            '-l', '@INPUT1@',
                            '-i', '@INPUT2@',
                            '-o', '@OUTPUT0@' ],
                install : true,
                install_dir : web_dir / lang)
endforeach
