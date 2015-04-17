load(configure)

qtCompileTest(opencl)

load(qt_parts)

!config_opencl {
    warning("OpenCL not found")
    SUBDIRS =
}
