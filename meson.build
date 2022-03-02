project('eos-metrics-instrumentation',
        'c',
        license: 'GPL-2.0-or-later',
        version: '0.0.0',
        default_options: ['c_std=c11', 'warning_level=2'],
        meson_version: '>= 0.52.0',
)

cc = meson.get_compiler('c')

common_deps = [
    dependency('eosmetrics-0', version: '>= 0.3'),
    dependency('flatpak'),
    dependency('gio-2.0'),
    dependency('glib-2.0', version: '>= 2.63.1'),
]
daemon_deps = common_deps + [
    dependency('json-glib-1.0'),
    dependency('libgtop-2.0'),
    dependency('ostree-1'),
]

py = import('python').find_installation('python3',
    modules: [
        # Meson can't express dbusmock >= 0.10 here
        'dbusmock',
        'dbus',
    ],
)

systemd_dep = dependency('systemd')

prefix = get_option('prefix')
libexec_dir = join_paths(prefix, get_option('libexecdir'))
instrumentation_cache_dir = get_option('localstatedir') / 'cache' / 'eos-metrics-instrumentation'

add_project_arguments(
    [
        '-DINSTRUMENTATION_CACHE_DIR="@0@"'.format(instrumentation_cache_dir),
    ],
    language: 'c',
)

subdir('data')
subdir('src')
subdir('tests')
subdir('tools')
