#include "psp_forge.h"
#include <math.h>

/* ========================================================================= */
/* 2D Collision Detection                                                    */
/* ========================================================================= */

bool forge_collide_rect_rect(ForgeRect a, ForgeRect b) {
    return (a.x < (b.x + b.w) &&
            (a.x + a.w) > b.x &&
            a.y < (b.y + b.h) &&
            (a.y + a.h) > b.y);
}

bool forge_collide_rect_circle(ForgeRect r, ForgeCircle c) {
    /* Find closest point on rectangle to circle center */
    float closest_x = c.x;
    float closest_y = c.y;

    if (c.x < r.x)         closest_x = r.x;
    else if (c.x > r.x + r.w) closest_x = r.x + r.w;

    if (c.y < r.y)         closest_y = r.y;
    else if (c.y > r.y + r.h) closest_y = r.y + r.h;

    float dx = c.x - closest_x;
    float dy = c.y - closest_y;
    return (dx * dx + dy * dy) <= (c.radius * c.radius);
}

bool forge_collide_point_rect(float px, float py, ForgeRect r) {
    return (px >= r.x && px <= (r.x + r.w) &&
            py >= r.y && py <= (r.y + r.h));
}

/* ========================================================================= */
/* 3D Collision Detection                                                    */
/* ========================================================================= */

bool forge_collide_aabb_aabb(ForgeAABB a, ForgeAABB b) {
    return (a.min.x <= b.max.x && a.max.x >= b.min.x &&
            a.min.y <= b.max.y && a.max.y >= b.min.y &&
            a.min.z <= b.max.z && a.max.z >= b.min.z);
}

bool forge_collide_sphere_sphere(ForgeSphere a, ForgeSphere b) {
    float dx = a.center.x - b.center.x;
    float dy = a.center.y - b.center.y;
    float dz = a.center.z - b.center.z;
    float dist_sq = dx * dx + dy * dy + dz * dz;
    float r_sum = a.radius + b.radius;
    return dist_sq <= (r_sum * r_sum);
}

bool forge_collide_aabb_sphere(ForgeAABB b, ForgeSphere s) {
    float closest_x = s.center.x;
    float closest_y = s.center.y;
    float closest_z = s.center.z;

    if (s.center.x < b.min.x) closest_x = b.min.x;
    else if (s.center.x > b.max.x) closest_x = b.max.x;

    if (s.center.y < b.min.y) closest_y = b.min.y;
    else if (s.center.y > b.max.y) closest_y = b.max.y;

    if (s.center.z < b.min.z) closest_z = b.min.z;
    else if (s.center.z > b.max.z) closest_z = b.max.z;

    float dx = s.center.x - closest_x;
    float dy = s.center.y - closest_y;
    float dz = s.center.z - closest_z;
    return (dx * dx + dy * dy + dz * dz) <= (s.radius * s.radius);
}

ForgeAABB forge_mesh_get_transformed_aabb(
    const ForgeMesh* mesh,
    float x, float y, float z,
    float sx, float sy, float sz
) {
    ForgeAABB box;
    if (!mesh) {
        box.min.x = box.min.y = box.min.z = 0.0f;
        box.max.x = box.max.y = box.max.z = 0.0f;
        return box;
    }

    box.min.x = x + mesh->aabb_min[0] * sx;
    box.min.y = y + mesh->aabb_min[1] * sy;
    box.min.z = z + mesh->aabb_min[2] * sz;

    box.max.x = x + mesh->aabb_max[0] * sx;
    box.max.y = y + mesh->aabb_max[1] * sy;
    box.max.z = z + mesh->aabb_max[2] * sz;

    return box;
}
