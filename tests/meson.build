gnome = import('gnome')


test_hwinfo_resources = gnome.compile_resources(
    'test-hwinfo-resources',
    'test-hwinfo-data/test-hwinfo.gresource.xml',
    source_dir: 'test-hwinfo-data',
    c_name: 'test_hwinfo',
)

test_hwinfo = executable(
    'test-hwinfo',
    [
        'test-hwinfo.c',
        test_hwinfo_resources,
    ],
    dependencies: [
        internal_library_dep,
    ],
    install: false,
)

test('test-hwinfo', test_hwinfo)
