kernel void updateParticles(global float *buf, float t, float dt, global float *info)
{
    int idx = get_global_id(0) * 2;
    if (t < 0.000001f) {
        buf[idx] = -1.0f;
        buf[idx + 1] = -1.0f;
    } else {
        float2 dir = (float2)(info[idx], info[idx + 1]);
        float2 speed = (float2)(info[idx + 2], info[idx + 3]);
        buf[idx] += dir.x * dt * speed.x;
        buf[idx + 1] += dir.y * dt * speed.y;
    }
}
