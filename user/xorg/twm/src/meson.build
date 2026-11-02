deftwmrc = custom_target(
    'deftwmrc.c',
    input: 'system.twmrc',
    output: 'deftwmrc.c',
    command: [
        find_program('gen_deftwmrc.sh'),
        '@OUTPUT@',
        '@INPUT@',
    ],
)

parser = custom_target(
    'gram.[ch]',
    input: [
        'gram.y',
    ],
    output: [
        'gram.c',
        'gram.h'
    ],
    command: [
        find_program('bison'),
        '-y',
        '-d',
        '-o',
        '@OUTPUT0@',
        '@INPUT@',
    ],
)

lexer = custom_target(
    'lex.c',
    input: [
        'lex.l',
        parser[1],
    ],
    output: [
        'lex.c',
    ],
    command: [
        find_program('flex'),
        '-o',
        '@OUTPUT@',
        '@INPUT0@',
    ],
)

twm = executable(
    'twm',
    [
        'add_window.c',
        'cursor.c',
        'events.c',
        'gc.c',
        'iconmgr.c',
        'icons.c',
        'list.c',
        'menus.c',
        'parse.c',
        'resize.c',
        'session.c',
        'twm.c',
        'util.c',
        deftwmrc,
        parser,
        lexer,
    ],
    dependencies: twm_dependencies,
    install: true,
)

configure_file(
    input: 'system.twmrc',
    output: 'system.twmrc',
    copy: true,
    install: true,
    install_dir: get_option('datadir') / 'X11/twm/',
)
