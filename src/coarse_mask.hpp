#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace riverfinder_native {
// 潜在有效列的积分图，以及所有仍能入榜的窗口所需列的矩形并集。
class CoarseMask {
    int width_, height_, stride_;
    std::vector<uint32_t> sums_;
    std::vector<int32_t> needed_;
    void bounds(int x,int z,int radius,int &l,int &t,int &r,int &b) const {
        l = std::max(0,x-radius);
        t = std::max(0,z-radius);
        r = std::min(width_,x+radius+1);
        b = std::min(height_,z+radius+1);
    }
  public:
    CoarseMask(int width,int height,const std::vector<uint8_t> &potential)
        : width_(width),height_(height),stride_(width+1),
          sums_(size_t(width+1)*(height+1)),needed_(sums_.size()) {
        for (int z = 0; z < height; ++z) {
            uint32_t row = 0;
            for (int x = 0; x < width; ++x) {
                row += potential[size_t(z)*width+x] != 0;
                sums_[size_t(z+1)*stride_+x+1] = sums_[size_t(z)*stride_+x+1]+row;
            }
        }
    }
    uint32_t count(int x,int z,int radius) const {
        int l,t,r,b;
        bounds(x,z,radius,l,t,r,b);
        return sums_[size_t(b)*stride_+r]-sums_[size_t(t)*stride_+r]-
               sums_[size_t(b)*stride_+l]+sums_[size_t(t)*stride_+l];
    }
    uint32_t countDisk(int x,int z,const std::vector<int> &halfWidths) const {
        uint32_t total = 0;
        int radius = int(halfWidths.size())/2;
        for (int i = 0; i < int(halfWidths.size()); ++i) {
            int row = z+i-radius;
            if (row < 0 || row >= height_)
                continue;
            int l = std::max(0,x-halfWidths[i]), r = std::min(width_,x+halfWidths[i]+1);
            if (l < r)
                total += sums_[size_t(row+1)*stride_+r]-sums_[size_t(row)*stride_+r]-
                         sums_[size_t(row+1)*stride_+l]+sums_[size_t(row)*stride_+l];
        }
        return total;
    }
    void include(int x,int z,int radius) {
        int l,t,r,b;
        bounds(x,z,radius,l,t,r,b);
        ++needed_[size_t(t)*stride_+l];
        --needed_[size_t(t)*stride_+r];
        --needed_[size_t(b)*stride_+l];
        ++needed_[size_t(b)*stride_+r];
    }
    void finish() {
        for (int z = 0; z < height_; ++z) {
            int32_t row = 0;
            for (int x = 0; x < width_; ++x) {
                row += needed_[size_t(z)*stride_+x];
                needed_[size_t(z)*stride_+x] = row+(z ? needed_[size_t(z-1)*stride_+x] : 0);
            }
        }
    }
    bool needs(int x,int z) const {
        return needed_[size_t(z)*stride_+x] != 0;
    }
};
}
