translatable_files = [
        'index.html',
        'verda-sxtelo.js',
]

languages = [
        'eo',
        'en',
]

data_files = [
        'verda-sxtelo.css',
        'jquery-1.9.1.min.js',
        'favicon.ico',
        'favicon.png',
        'invite.png',
        'message-alert-sound.ogg',
        'message-alert-sound.mp3',
        'shout-sound.ogg',
        'shout-sound.mp3',
        'turn-sound.ogg',
        'turn-sound.mp3',
        'sound-on.png',
        'sound-off.png',
]

web_dir = get_option('datadir') / 'web'

install_data(sources : data_files, install_dir : web_dir)
             
install_data('htaccess', install_dir : web_dir, rename : '.htaccess')

perl = find_program('perl')

foreach lang : languages
  install_data(lang / 'help.html', install_dir : web_dir / lang)
  subdir(lang)
endforeach
