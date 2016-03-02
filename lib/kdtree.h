#pragma once

#include "triangle.h"
#include "clipping.h"
#include <memory>
#include <algorithm>


//
// KDTree for D = 3
//
template<unsigned int LEAF_CAPACITY=10>
class KDTree {

    // TODO: Change the structure of the node s.t. it fits into a cpu cache
    // line of 64 bytes.
    struct Node {
        // a leaf contains triangle
        Node(Box bbox, Axis ax, Triangles&& triangles)
            : bbox(bbox), splitaxis(ax), triangles(triangles)
            {}

        // an inner node contains only bbox and splitting axis
        Node(Box bbox, Axis ax,
                const std::shared_ptr<const Node>& lft,
                const std::shared_ptr<const Node>& rht)
            : bbox(bbox), splitaxis(ax), lft_(lft), rht_(rht)
            {}

        Box bbox;
        Axis splitaxis;
        Triangles triangles;

        std::shared_ptr<const Node> lft_;
        std::shared_ptr<const Node> rht_;
    };

    explicit KDTree(const std::shared_ptr<const Node>& node)
        : root_(node)
        {}

public:
    // Build up the tree.
    KDTree(Triangles triangles) {
        if (triangles.size() == 0) {
            return;
        }

        // compute bounding box of all triangles
        Box box = triangles[0].bbox();
        for (auto it = triangles.begin() + 1; it != triangles.end(); ++it) {
            box = box + it->bbox();
        }
        assert(box.min < box.max);

        // choose the longest axis in the box
        Axis axis;
        float dx = box.max.x - box.min.x;
        float dy = box.max.y - box.min.y;
        float dz = box.max.z - box.min.z;
        if (dx > dy && dx > dz) {
            axis = Axis::X;
        } else if (dy > dz) {
            axis = Axis::Y;
        } else {
            axis = Axis::Z;
        }

        // Do we have to partition at all?
        if (triangles.size() <= LEAF_CAPACITY) {
            root_ = std::make_shared<const Node>(
                box, axis, std::move(triangles));
            return;
        }

        auto split = split_at_spatial_median(axis, box, std::move(triangles));
        root_ = std::make_shared<const Node>(
            box, axis, split.first.root_, split.second.root_);
    }

    // properties

    bool empty() const { return !root_; }
    bool is_leaf() const {return !empty() && !root_->lft_ && !root_->rht_;}

    KDTree left() const { assert(!empty()); return KDTree(root_->lft_); }
    KDTree right() const { assert(!empty()); return KDTree(root_->rht_); }

    Box bbox() const { assert(!empty()); return root_->bbox; }
    Axis splitaxis() const { assert(!empty()); return root_->splitaxis; }
    const Triangles& triangles() const {
        assert(!empty());
        assert(is_leaf());
        return root_->triangles;
    }

    size_t height() const {
        if (empty()) {
            return 0;
        }
        return 1 + std::max(left().height(), right().height());
    }

    // algorithms

    //
    // Args:
    //   r - distance from ray to found triangle
    //   s, t - barycentric coordinates of intersection point
    //
    // Not liking the return pointer used as optional here? Me neither, but I
    // don't have a better idea.
    //
    const Triangle* intersect(
        const Ray& ray, float& r, float& s, float& t) const
    {
        if (empty()) {
            return nullptr;
        }

        if (!ray_box_intersection(ray, bbox())) {
            return nullptr;
        }

        // we are at the bottom in a leaf
        if (is_leaf()) {
            return intersect(ray, triangles(), r, s, t);
        }

        // try to find a triangle in both subtrees
        float left_r = -1, left_s = -1, left_t = -1;
        auto left_tri = left().intersect(ray, left_r, left_s, left_t);

        float right_r = -1, right_s = -1, right_t = -1;
        auto right_tri = right().intersect(ray, right_r, right_s, right_t);

        if (left_tri && (!right_tri || left_r < right_r)) {
            r = left_r;
            s = left_s;
            t = left_t;
            return left_tri;
        } else if (right_tri) {
            r = right_r;
            s = right_s;
            t = right_t;
            return right_tri;
        }

        return nullptr;
    }

    template<unsigned int LEAF_CAPACITY_>
    friend class KDTreeTester;

private:
    std::pair<KDTree, KDTree> split_at_triangles_median(
        const Axis axis, Triangles triangles)
    {
        // Find median along axis and partition triangles (linear complexity).
        // Triangles are sorted along chosen axis at midpoint.
        //
        // TODO: Does nth_element invalidate iterators? If not, store mid_it
        // before and use it.
        std::nth_element(
            triangles.begin(),
            triangles.begin() + triangles.size() / 2,
            triangles.end(),
            [axis](const Triangle& tria, const Triangle& trib) {
                return tria.midpoint()[axis] < trib.midpoint()[axis];
            });
        auto mid_it = triangles.begin() + triangles.size() / 2;

        Triangles rht_triangles(mid_it, triangles.end());
        Triangles& lft_triangles = triangles;
        triangles.erase(mid_it, triangles.end());

        return std::make_pair(
            KDTree(std::move(lft_triangles)),
            KDTree(std::move(rht_triangles)));
    }

    std::pair<KDTree, KDTree> split_at_spatial_median(
        const Axis axis, const Box& bbox, Triangles triangles)
    {
        float min = bbox.min[axis];
        float max = bbox.max[axis];

        Triangles lft_triangles;
        Triangles rht_triangles;

        float axis_midpt;

        // continue splitting to get two real subsets
        while (lft_triangles.empty() || rht_triangles.empty()) {
            if (!lft_triangles.empty()) {
                max = axis_midpt;
                lft_triangles.clear();
            } else if (!rht_triangles.empty()) {
                min = axis_midpt;
                rht_triangles.clear();
            }

            axis_midpt = (min + max) / 2.f;
            for (auto& triangle : triangles) {
                if (triangle.midpoint()[axis] < axis_midpt) {
                    lft_triangles.push_back(triangle);
                } else {
                    rht_triangles.push_back(triangle);
                }
            }
        }

        return std::make_pair(
            std::move(lft_triangles), std::move(rht_triangles));
    }

    const Triangle* intersect(
        const Ray& ray, const Triangles& triangles,
        float& min_r, float& min_s, float& min_t) const
    {
        min_r = -1;
        const Triangle* res = nullptr;
        float r, s, t;
        for (const auto& triangle : triangles) {
            bool intersects = triangle.intersect(ray, r, s, t);
            if (intersects) {
                if (!res || r < min_r) {
                    min_r = r;
                    min_s = s;
                    min_t = t;
                    res = &triangle;
                }
            }
        }

        return res;
    }

    std::shared_ptr<const Node> root_;
};

//
// "On building fast kd-Trees for Ray Tracing, and on doing that in O(N log N)"
// by Ingo Wald and Vlastimil Havran
//

// Cf. 5.2, Table 1
static constexpr int COST_TRAVERSAL = 15;
static constexpr int COST_INTERSECTION = 20;


struct Plane {
    Axis ax;        // 1 byte
    float coord;    // 4 bytes
                    // = 8 bytes
};

inline std::pair<Box, Box> split(const Box& box, const Plane plane) {
    assert(box.min[plane.ax] <= plane.coord);
    assert(plane.coord <= box.max[plane.ax]);

    Vec lmax = box.max;
    lmax[plane.ax] = plane.coord;
    Vec rmin = box.min;
    rmin[plane.ax] = plane.coord;

    return {{box.min, lmax}, {rmin, box.max}};
}

// Cost function Bias
inline float lambda(size_t num_ltris, size_t num_rtris) {
    if (num_ltris == 0 || num_rtris == 0) {
        return 0.8f;
    }
    return 1;
}

//
// Cost function of splitting box b at a given plane p
//
// Args:
//   {l,r}area_ratio - ratio of the surface area of the left resp. right
//     box over the box
//   num_{l,r}tris - number of triangles in the left resp. right box
//
// Return:
//   cost to split the box
//
inline float cost(
    float larea_ratio, float rarea_ratio, size_t num_ltris, size_t num_rtris)
{
    return lambda(num_ltris, num_rtris) *
        (COST_TRAVERSAL + COST_INTERSECTION *
            (larea_ratio * num_ltris + rarea_ratio * num_rtris));
}

enum class Dir { LEFT, RIGHT };

//
// SAH function
//
// Args:
//   p - splitting plane
//   box - AABB to split
//   num_{l,r}tris - number of triangles in the left resp. right box produces
//     by splitting `box` at `p`
//   num_planar_tris - number of triangles lying in the plane `p`
//
// Return:
//   cost to split the box + if the planar triangles should be appended to the
//   lhs or rhs of the box
//
inline std::pair<float /*cost*/, Dir> surface_area_heuristics(
    Plane p, const Box& box,
    size_t num_ltris, size_t num_rtris, size_t num_planar_tris)
{
    Box lbox, rbox;
    std::tie(lbox, rbox) = split(box, p);
    float area = box.surface_area();
    float larea_ratio = lbox.surface_area()/area;
    float rarea_ratio = rbox.surface_area()/area;

    auto lpcost = cost(
        larea_ratio, rarea_ratio,
        num_ltris + num_planar_tris, num_rtris);
    auto rpcost = cost(
        larea_ratio, rarea_ratio,
        num_ltris, num_planar_tris + num_rtris);

    if (lpcost < rpcost) {
        return {lpcost, Dir::LEFT};
    } else {
        return {rpcost, Dir::RIGHT};
    }
}


class FastKDTree {

    enum class NodeType : char { INNER = 0, LEAF = 1 };

    struct Node {
        NodeType type;
    };

    struct InnerNode : public Node {
        InnerNode(
            Plane p, std::unique_ptr<Node> lft, std::unique_ptr<Node> rht)
        : Node{NodeType::INNER}
        , p(p)
        , lft(std::move(lft))
        , rht(std::move(rht))
        {}

        Plane p;
        std::unique_ptr<Node> lft;
        std::unique_ptr<Node> rht;
    };

    struct Leaf : public Node {
        Leaf(Triangles tris)
        : Node{NodeType::LEAF}
        , tris(std::move(tris))
        {}

        Triangles tris;
    };

public:
    FastKDTree(Triangles tris) {
        assert(tris.size() > 0);

        Box box = tris.front().bbox();
        for (auto it = tris.begin() + 1; it != tris.end(); ++it) {
            box = box + it->bbox();
        }
        root_ = build(std::move(tris), box);
    }

    std::unique_ptr<Node> build(Triangles tris, const Box& box) {
        if (terminate(tris, box)) {
            return std::make_unique<Leaf>(tris);
        }

        Plane p;
        std::tie(std::ignore, p, std::ignore) = find_plane(tris, box);

        Box lft_box, rht_box;
        std::tie(lft_box, rht_box) = split(box, p);
        Triangles lft_tris, rht_tris;
        for (const auto& tri : tris) {
            if (tri.intersect(lft_box)) {
                lft_tris.push_back(tri);
            }
            if (tri.intersect(rht_box)) {
                rht_tris.push_back(tri);
            }
        }

        return std::make_unique<InnerNode>(p,
            build(std::move(lft_tris), lft_box),
            build(std::move(rht_tris), rht_box));
    }

    friend class FastKDTreeTester;

private:
    bool terminate(const Triangles& /*tris*/, const Box& /*box*/) const {
        // TODO: Implement
        return true;
    }

    static constexpr int STARTING = 2;
    static constexpr int ENDING = 0;
    static constexpr int PLANAR = 1;

    struct Event {
        const Triangle* tri;
        float point;
        int type;
    };

    std::tuple<float /*cost*/, Plane, Dir /*side*/>
    find_plane(const Triangles& tris, const Box& box) const {
        float min_cost = std::numeric_limits<float>::max();
        Plane min_plane;
        Dir min_side;

        Axis ax = Axis::X;
        for (int k = 0; k < 3; ++k, ++ax) {
            // generate events
            std::vector<Event> events;
            for (const auto& tri : tris) {
                auto clipped_box = triangle_clip_aabb(tri, box);
                if (clipped_box.is_planar(ax)) {
                    events.push_back(
                        Event{&tri, clipped_box.min[ax], PLANAR});
                } else {
                    events.push_back(
                        Event{&tri, clipped_box.min[ax], STARTING});
                    events.push_back(
                        Event{&tri, clipped_box.max[ax], ENDING});
                }
            }

            std::sort(events.begin(), events.end(),
                [](const Event& e1, const Event& e2) {
                    return e1.point < e2.point ||
                        (eps_zero(e1.point - e2.point) && e1.type < e2.type);
                });

            // sweep
            int num_ltris = 0, num_planar_tris = 0, num_rtris = tris.size();
            for (size_t i = 0; i < events.size(); ) {
                auto& event = events[i];

                auto p = event.point;
                int point_starting = 0;
                int point_ending = 0;
                int point_planar = 0;

                while (i < events.size() && events[i].point == p &&
                    events[i].type == ENDING)
                {
                    point_ending += 1;
                    i += 1;
                }
                while (i < events.size() && events[i].point == p &&
                    events[i].type == PLANAR)
                {
                    point_planar += 1;
                    i += 1;
                }
                while (i < events.size() && events[i].point == p &&
                    events[i].type == STARTING)
                {
                    point_ending += 1;
                    i += 1;
                }

                num_planar_tris = point_planar;
                num_rtris -= point_planar + point_ending;

                Plane plane{static_cast<Axis>(k), p};

                float cost;
                Dir side;
                std::tie(cost, side) = surface_area_heuristics(
                    plane, box, num_ltris, num_rtris, num_planar_tris);

                if (cost < min_cost) {
                    min_cost = cost;
                    min_plane = plane;
                    min_side = side;
                }

                num_ltris += point_starting + point_planar;
                num_planar_tris = 0;
            }
        }

        // classify

        return {min_cost, min_plane, min_side};
    }

private:
    std::unique_ptr<Node> root_;
};
