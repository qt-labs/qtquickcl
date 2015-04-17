// Based on https://code.google.com/p/opencl-book-samples/source/browse/trunk/src/Chapter_14/histogram

#pragma OPENCL EXTENSION cl_khr_local_int32_base_atomics : enable

constant sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

kernel void histogram(image2d_t img, int num_pixels_per_workitem, global uint *buf)
{
    int local_size = get_local_size(0) * get_local_size(1);
    int item_offset = get_local_id(0) + get_local_id(1) * get_local_size(0);
    local uint tmp[256];

    int i = 0, j = 256;
    do {
        if (item_offset < j)
            tmp[item_offset + i] = 0;
        j -= local_size;
        i += local_size;
    } while (j > 0);

    barrier(CLK_LOCAL_MEM_FENCE);

    int x, image_width = get_image_width(img), image_height = get_image_height(img);
    for (i = 0, x = get_global_id(0); i < num_pixels_per_workitem; ++i, x += get_global_size(0)) {
        if (x < image_width && get_global_id(1) < image_height) {
            float4 clr = read_imagef(img, sampler, (int2)(x, get_global_id(1)));
            if (clr.w > 0.99f) {
                float v = (clr.x + clr.y + clr.z) / 3.0f;
                atom_inc(&tmp[convert_ushort_sat(min(v, 1.0f) * 255.0f)]);
            }
        }
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    int group_offset = (get_group_id(0) + get_group_id(1) * get_num_groups(0)) * 256;
    i = 0;
    j = 256;
    do {
        if (item_offset < j)
            buf[group_offset + item_offset + i] = tmp[item_offset + i];
        j -= local_size;
        i += local_size;
    } while (j > 0);
}

kernel void sum_histogram(global uint *buf, int num_groups, global uint *result)
{
    int idx = get_global_id(0) + get_group_id(0);
    uint v = 0;
    int group_offset = 0, n = num_groups;
    while (--n >= 0) {
        v += buf[group_offset + idx];
        group_offset += 256;
    }
    result[idx] = v;
}
