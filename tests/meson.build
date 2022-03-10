test_sources = {
    'test-hwinfo': [],
}

foreach name, sources : test_sources
    test(name,
        executable(name,
            sources + ['@0@.c'.format(name)],
            dependencies: internal_library_dep,
            install: false,
        )
    )
endforeach
