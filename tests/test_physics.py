import unittest

def compute_transformed_aabb(aabb_min, aabb_max, x, y, z, sx, sy, sz):
    tx1 = aabb_min[0] * sx
    tx2 = aabb_max[0] * sx
    min_x = x + (tx1 if tx1 < tx2 else tx2)
    max_x = x + (tx1 if tx1 > tx2 else tx2)

    ty1 = aabb_min[1] * sy
    ty2 = aabb_max[1] * sy
    min_y = y + (ty1 if ty1 < ty2 else ty2)
    max_y = y + (ty1 if ty1 > ty2 else ty2)

    tz1 = aabb_min[2] * sz
    tz2 = aabb_max[2] * sz
    min_z = z + (tz1 if tz1 < tz2 else tz2)
    max_z = z + (tz1 if tz1 > tz2 else tz2)

    return (min_x, min_y, min_z), (max_x, max_y, max_z)

def collide_aabb(box_a, box_b):
    (a_min, a_max) = box_a
    (b_min, b_max) = box_b
    return (
        a_min[0] <= b_max[0] and a_max[0] >= b_min[0] and
        a_min[1] <= b_max[1] and a_max[1] >= b_min[1] and
        a_min[2] <= b_max[2] and a_max[2] >= b_min[2]
    )

class TestPhysicsAABB(unittest.TestCase):
    def test_positive_scale(self):
        b_min, b_max = compute_transformed_aabb([-1, -2, -3], [1, 2, 3], 10, 20, 30, 2, 2, 2)
        self.assertEqual(b_min, (8, 16, 24))
        self.assertEqual(b_max, (12, 24, 36))
        self.assertTrue(b_min[0] <= b_max[0])
        self.assertTrue(b_min[1] <= b_max[1])
        self.assertTrue(b_min[2] <= b_max[2])

    def test_negative_scale(self):
        # Flipping across axes with negative scale should still maintain min <= max
        b_min, b_max = compute_transformed_aabb([-1, -2, -3], [1, 2, 3], 10, 20, 30, -2, -3, -1)
        self.assertEqual(b_min, (8, 14, 27))
        self.assertEqual(b_max, (12, 26, 33))
        self.assertTrue(b_min[0] <= b_max[0])
        self.assertTrue(b_min[1] <= b_max[1])
        self.assertTrue(b_min[2] <= b_max[2])

    def test_asymmetric_bounds_negative_scale(self):
        # Asymmetric bounds min=[-1, 0, -2], max=[5, 10, 4] with sx=-1
        b_min, b_max = compute_transformed_aabb([-1, 0, -2], [5, 10, 4], 0, 0, 0, -1, 1, 1)
        self.assertEqual(b_min[0], -5)
        self.assertEqual(b_max[0], 1)
        self.assertTrue(b_min[0] <= b_max[0])

    def test_collision_with_negative_scale(self):
        box1 = compute_transformed_aabb([-1, -1, -1], [1, 1, 1], 0, 0, 0, -1, -1, -1)
        box2 = compute_transformed_aabb([-1, -1, -1], [1, 1, 1], 1.5, 0, 0, 1, 1, 1)
        box3 = compute_transformed_aabb([-1, -1, -1], [1, 1, 1], 5.0, 0, 0, 1, 1, 1)
        self.assertTrue(collide_aabb(box1, box2))
        self.assertFalse(collide_aabb(box1, box3))

if __name__ == "__main__":
    unittest.main()
