#include "filters/bitmap/TraceFilter.h"
#include <array>

void TraceFilter::applyTyped(const Bitmap &in, PathSet &out) const
{
    out.paths.clear();
    out.color = Color(1.0f, 1.0f, 1.0f, 1.0f);

    if (in.width_px <= 0 || in.height_px <= 0 || in.pixels.empty())
    {
        out.computeAABB();
        return;
    }

    const int width = static_cast<int>(in.width_px);
    const int height = static_cast<int>(in.height_px);
    const uint8_t threshold = 128;

    std::vector<bool> visited(static_cast<size_t>(width) * static_cast<size_t>(height), false);

    // 8-connected neighborhood offsets (clockwise from right)
    // Order: E, SE, S, SW, W, NW, N, NE
    const std::array<int, 8> dx = { 1,  1,  0, -1, -1, -1,  0,  1};
    const std::array<int, 8> dy = { 0,  1,  1,  1,  0, -1, -1, -1};

    auto getPixel = [&](int x, int y) -> uint8_t {
        if (x < 0 || x >= width || y < 0 || y >= height)
            return 0;
        return in.pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
    };

    auto isVisited = [&](int x, int y) -> bool {
        if (x < 0 || x >= width || y < 0 || y >= height)
            return true;
        return visited[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
    };

    auto setVisited = [&](int x, int y) {
        if (x >= 0 && x < width && y >= 0 && y < height)
            visited[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = true;
    };

    auto isForeground = [&](int x, int y) -> bool {
        return getPixel(x, y) <= threshold;
    };

    // Trace a single contour starting from (startX, startY)
    auto traceContour = [&](int startX, int startY) -> Path {
        Path contour;
        contour.closed = false;

        int x = startX;
        int y = startY;

        // Find the entry direction (look for background pixel to start from)
        int dir = 0; // Start looking east
        for (int i = 0; i < 8; ++i) {
            int checkDir = (7 - i) % 8; // Start from west and go counter-clockwise
            int nx = x + dx[checkDir];
            int ny = y + dy[checkDir];
            if (!isForeground(nx, ny)) {
                dir = checkDir;
                break;
            }
        }

        int firstX = x;
        int firstY = y;
        bool first = true;

        do {
            // Add current pixel to contour
            float px = (static_cast<float>(x) + 0.5f) * in.pixel_size_mm;
            float py = (static_cast<float>(y) + 0.5f) * in.pixel_size_mm;
            contour.points.emplace_back(px, py);
            setVisited(x, y);

            // Moore-Neighbor tracing: search for next boundary pixel
            // Start from the previous direction + 5 (which is -3, or 135 degrees back)
            // This ensures we search counter-clockwise around the current pixel
            int searchStart = (dir + 5) % 8;
            bool found = false;

            for (int i = 0; i < 8; ++i) {
                int checkDir = (searchStart + i) % 8;
                int nx = x + dx[checkDir];
                int ny = y + dy[checkDir];

                if (isForeground(nx, ny) && !isVisited(nx, ny)) {
                    // Found next boundary pixel
                    x = nx;
                    y = ny;
                    dir = checkDir;
                    found = true;
                    break;
                }
            }

            if (!found) {
                // Reached end of open path
                break;
            }

            // Check if we've returned to the start
            if (!first && x == firstX && y == firstY) {
                // Closed loop - mark as closed
                contour.closed = true;
                break;
            }
            first = false;

        } while (true);

        return contour;
    };

    // Scan the image to find contours
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Find unvisited foreground pixel
            if (isForeground(x, y) && !isVisited(x, y)) {
                Path contour = traceContour(x, y);

                // Only add contours with at least 2 points
                if (contour.points.size() >= 2) {
                    out.paths.push_back(std::move(contour));
                }
            }
        }
    }

    out.computeAABB();
}


