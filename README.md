QtQuickCL - Write kernels, not boilerplate

QtQuickCL is a research module for Qt 5 that enables easily creating Qt Quick
items that execute OpenCL kernels and use OpenGL resources as their input or
output. The functionality is minimal but powerful.

For example, a Qt Quick item executing an OpenCL kernel on a texture provider,
meaning an Image element or even an entire sub-tree of the Qt Quick scene, and
rendering the resulting OpenGL texture gets reduced to the following:

    static const char *openclSrc =
            "__kernel void Kernel(__read_only image2d_t imgIn, __write_only image2d_t imgOut) {\n"
            ...

    class CLRunnable : public QQuickCLImageRunnable
    {
    public:
        CLRunnable(QQuickCLItem *item)
            : QQuickCLImageRunnable(item)
        {
            m_clProgram = item->buildProgram(openclSrc);
            m_clKernel = clCreateKernel(m_clProgram, "Kernel", 0);
        }

        ~CLRunnable() {
            clReleaseKernel(m_clKernel);
            clReleaseProgram(m_clProgram);
        }

        void runKernel(cl_mem inImage, cl_mem outImage, const QSize &size) Q_DECL_OVERRIDE {
            clSetKernelArg(m_clKernel, 0, sizeof(cl_mem), &inImage);
            clSetKernelArg(m_clKernel, 1, sizeof(cl_mem), &outImage);
            const size_t workSize[] = { size_t(size.width()), size_t(size.height()) };
            clEnqueueNDRangeKernel(commandQueue(), m_clKernel, 2, 0, workSize, 0, 0, 0, 0);
        }

    private:
        cl_program m_clProgram;
        cl_kernel m_clKernel;
    };

    class CLItem : public QQuickCLItem
    {
        Q_OBJECT
        Q_PROPERTY(QQuickItem *source READ source WRITE setSource)

    public:
        CLItem() : m_source(0) { }

        QQuickCLRunnable *createCL() Q_DECL_OVERRIDE { return new CLRunnable(this); }

        QQuickItem *source() const { return m_source; }
        void setSource(QQuickItem *source) { m_source = source; update(); }

    private:
        QQuickItem *m_source;
    };

All the CL-GL interop, including the selection of the correct CL platform and
device, is taken care of by the module. The QQuickCLItem - QQuickCLRunnable
split ensures easy and safe CL and GL resource management even when threaded
rendering is in use.

Besides the minimal and lightweight framework, examples are provided for the
following common use cases:

   * GL texture -> GL texture (image processing, i.e. ShaderEffect but with CL)

   * GL texture -> arbitrary data that gets visualized through Qt Quick
     (f.ex. histogram)

   * Non-GL data -> GL buffer with vertex data which is used in OpenGL rendering
     without any additional readbacks or copies (e.g. particle simulation)

The intention for these is not merely to test and demo the classes, but to serve
as a reference for integrating Qt Quick and OpenCL and to help getting started
with CL development.
