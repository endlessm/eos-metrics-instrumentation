# Systemd unit
configure_file(
    input: 'eos-metrics-instrumentation.service.in',
    output: 'eos-metrics-instrumentation.service',
    configuration: configuration_data({
        'libexecdir': libexec_dir,
    }),
    install: true,
    install_dir: systemd_dep.get_variable(pkgconfig: 'systemdsystemunitdir'),
)

# tmpfiles rules
configure_file(
    input: 'eos-metrics-instrumentation.conf.in',
    output: 'eos-metrics-instrumentation.conf',
    configuration: configuration_data({
        'instrumentationcachedir': instrumentation_cache_dir,
    }),
    install: true,
    install_dir: systemd_dep.get_variable(pkgconfig: 'tmpfilesdir'),
)

if get_option('crash_metrics').enabled()
    # sysctl configuration
    configure_file(
        input: '90-eos-crash-metrics.conf.in',
        output: '90-eos-crash-metrics.conf',
        configuration: configuration_data({
            'libexecdir': libexec_dir,
        }),
        install: true,
        install_dir: systemd_dep.get_variable(pkgconfig: 'sysctldir'),
    )
endif
