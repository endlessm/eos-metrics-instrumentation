internal_library = static_library('libemi',
    sources: [
        'eins-hwinfo.h',
        'eins-hwinfo.c',
        'eins-boottime-source.h',
        'eins-boottime-source.c',
    ],
    dependencies: daemon_deps,
    install: false,
)

internal_library_dep = declare_dependency(
    dependencies: daemon_deps,
    link_with: internal_library,
    include_directories: include_directories('.'),
)

daemon = executable('eos-metrics-instrumentation',
    dependencies: [
        internal_library_dep,
    ],
    sources: [
        'eos-metrics-instrumentation.c',
    ],
    install: true,
    install_dir: libexec_dir,
)

crash_metrics = executable('eos-crash-metrics',
    dependencies: common_deps,
    sources: [
        'eos-crash-metrics.c',
    ],
    install: true,
    install_dir: libexec_dir,
)
