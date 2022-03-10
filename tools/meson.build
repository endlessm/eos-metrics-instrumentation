label_location = configure_file(
    input: 'eos-label-location.in',
    output: '@BASENAME@',
    configuration: {
        'sysconfdir': get_option('sysconfdir'),
    },
    install: true,
    install_dir: get_option('bindir'),
)
