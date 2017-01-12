#include "../lib/triangle.h"
#include "../lib/xorshift.h"
#include "helper.h"

#include <catch.hpp>

TEST_CASE("Test Triangle normal", "[triangle]") {
    static constexpr int NUM_SAMPLES = 100;

    for (int j = 0; j < NUM_SAMPLES; ++j) {
        Triangle triangle = random_triangle();

        Vec normal = triangle.normal;

        // Verify unit length of normal.
        float length = normal.Length();
        REQUIRE(length == Approx(1.f));

        // Verify normal is perpendicular to edges of triangle.
        float cos_u = normal * triangle.u.Normalize();
        float cos_v = normal * triangle.v.Normalize();
        REQUIRE(cos_u == Approx(0.f));
        REQUIRE(cos_v == Approx(0.f));
    }
}

TEST_CASE("Test interpolate triangle normal", "[triangle]") {
    static constexpr int NUM_SAMPLES = 100;
    static xorshift64star<float> uniform{4};

    for (int j = 0; j < NUM_SAMPLES; ++j) {
        Triangle triangle = test_triangle(
            random_vec(), random_vec(), random_vec(), random_vec().Normalize(),
            random_vec().Normalize(), random_vec().Normalize());

        float r = uniform();
        float s = uniform();
        float t = uniform();
        while (r + s + t > 1.0f) {
            r = uniform();
            s = uniform();
            t = uniform();
        }
        Vec normal = triangle.interpolate_normal(r, s, t);

        // Verify unit length of normal.
        float length = normal.Length();
        REQUIRE(length == Approx(1.f));
    }
}

TEST_CASE("Test interpolate triangle normal with trivial case", "[triangle]") {
    static constexpr int NUM_SAMPLES = 100;
    static xorshift64star<float> uniform{4};

    auto normal = random_vec().Normalize();
    Triangle triangle = test_triangle(random_vec(), random_vec(), random_vec(),
                                      normal, normal, normal);
    for (int j = 0; j < NUM_SAMPLES; ++j) {

        float r = uniform();
        float s = uniform();
        float t = uniform();
        while (r + s + t > 1.0f) {
            r = uniform();
            s = uniform();
            t = uniform();
        }
        Vec interpolated_normal = triangle.interpolate_normal(r, s, t);

        // Verify unit length of normal.
        float length = interpolated_normal.Length();
        REQUIRE(length == Approx(1.f));

        // Verify that interpolated normal is equal to actual normal
        REQUIRE(interpolated_normal.x == Approx(normal.x));
        REQUIRE(interpolated_normal.y == Approx(normal.y));
        REQUIRE(interpolated_normal.z == Approx(normal.z));
    }
}