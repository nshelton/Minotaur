#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <iostream>
#include "../src/utils/nanoflann.hpp"
#include "../src/core/Vec2.h"
#include <type_traits>


struct PointCloud
{
    using coord_t = float;

    std::vector<Vec2> pts;

    // Must return the number of data points
    inline size_t kdtree_get_point_count() const { return pts.size(); }

    // Returns the dim'th component of the idx'th point in the class:
    // Since this is inlined and the "dim" argument is typically an immediate
    // value, the
    //  "if/else's" are actually solved at compile time.
    inline float kdtree_get_pt(const size_t idx, const size_t dim) const
    {
        if (dim == 0)
            return pts[idx].x;
        else
            return pts[idx].y;
    }

    // Optional bounding-box computation: return false to default to a standard
    // bbox computation loop.
    //   Return true if the BBOX was already computed by the class and returned
    //   in "bb" so it can be avoided to redo it again. Look at bb.size() to
    //   find out the expected dimensionality (e.g. 2 or 3 for point clouds)
    template <class BBOX>
    bool kdtree_get_bbox(BBOX& /* bb */) const  { return false;}
};

 
void generateRandomPointCloudRanges(
    PointCloud& pc, const size_t N, const float max_range_x, const float max_range_y)
{
    // Generating Random Point Cloud
    pc.pts.resize(N);
    for (size_t i = 0; i < N; i++)
    {
        pc.pts[i].x = max_range_x * (rand() % 1000) / T(1000);
        pc.pts[i].y = max_range_y * (rand() % 1000) / T(1000);
    }
}

template <typename T>
void generateRandomPointCloud(
    PointCloud& pc, const size_t N, const float max_range = 10)
{
    generateRandomPointCloudRanges(pc, N, max_range, max_range);
}


inline void dump_mem_usage()
{
    FILE* f = fopen("/proc/self/statm", "rt");
    if (!f) return;
    char   str[300];
    size_t n = fread(str, 1, 200, f);
    str[n]   = 0;
    printf("MEM: %s\n", str);
    fclose(f);
}

void kdtree_demo(const size_t N)
{
    PointCloud cloud;

    // construct a kd-tree index:
    using my_kd_tree_t = nanoflann::KDTreeSingleIndexDynamicAdaptor<
        nanoflann::L2_Simple_Adaptor<float, PointCloud>,
        PointCloud, 2 /* dim */
        >;

    dump_mem_usage();

    my_kd_tree_t index(3 /*dim*/, cloud, {10 /* max leaf */});


    float query_pt[3] = {0.5, 0.5};

    // add points in chunks at a time
    size_t chunk_size = 100;
    for (size_t i = 0; i < N; i = i + chunk_size)
    {
        size_t end = std::min<size_t>(i + chunk_size, N - 1);
        // Inserts all points from [i, end]
        index.addPoints(i, end);
    }

    // remove a point
    size_t removePointIndex = N - 1;
    index.removePoint(removePointIndex);

    dump_mem_usage();
    {
        std::cout << "Searching for 1 element..." << std::endl;
        // do a knn search
        const size_t num_results = 1;
        size_t ret_index;
        float out_dist_sqr;
        nanoflann::KNNResultSet<float> resultSet(num_results);
        resultSet.init(&ret_index, &out_dist_sqr);
        index.findNeighbors(resultSet, query_pt, {10});

        std::cout << "knnSearch(nn=" << num_results << "): \n";
        std::cout << "ret_index=" << ret_index
                  << " out_dist_sqr=" << out_dist_sqr << std::endl;
        std::cout << "point: ("
                  << "point: (" << cloud.pts[ret_index].x << ", "
                  << cloud.pts[ret_index].y << ")" << std::endl;
        std::cout << std::endl;
    }
    {
        // do a knn search searching for more than one result
        const size_t num_results = 5;
        std::cout << "Searching for " << num_results << " elements"
                  << std::endl;
        size_t ret_index[num_results];
        float out_dist_sqr[num_results];
        nanoflann::KNNResultSet<float> resultSet(num_results);
        resultSet.init(ret_index, out_dist_sqr);
        index.findNeighbors(resultSet, query_pt);

        std::cout << "knnSearch(nn=" << num_results << "): \n";
        std::cout << "Results: " << std::endl;
        for (size_t i = 0; i < resultSet.size(); ++i)
        {
            std::cout << "#" << i << ",\t"
                      << "index: " << ret_index[i] << ",\t"
                      << "dist: " << out_dist_sqr[i] << ",\t"
                      << "point: (" << cloud.pts[ret_index[i]].x << ", "
                      << cloud.pts[ret_index[i]].y <<  ")" << std::endl;
        }
        std::cout << std::endl;
    }
    {
        // Unsorted radius search:
        std::cout << "Unsorted radius search" << std::endl;
        const float radiusSqr = 1;
        std::vector<nanoflann::ResultItem<size_t, float>> indices_dists;
        nanoflann::RadiusResultSet<float, size_t> resultSet(
            radiusSqr, indices_dists);

        index.findNeighbors(resultSet, query_pt);

        nanoflann::ResultItem<size_t, float> worst_pair =
            resultSet.worst_item();
        std::cout << "Worst pair: idx=" << worst_pair.first
                  << " dist=" << worst_pair.second << std::endl;
        std::cout << "point: (" << cloud.pts[worst_pair.first].x << ", "
                  << cloud.pts[worst_pair.first].y << ")" << std::endl;
        std::cout << std::endl;
    }
}

TEST(kdtree, Basic)
{
    kdtree_demo(1000000);
}