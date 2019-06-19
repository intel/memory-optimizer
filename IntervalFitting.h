/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2019 Intel Corporation
 *
 * Authors: Yao Yuan <yuan.yao@intel.com>
 *
 */

#include <list>
#include <map>
#include <climits>
#include <algorithm>

template<typename Tx, typename Ty, int TMaxSample>
class IntervalFitting
{
 private:
    struct DataPair {
        Tx x;
        Ty y;
    };

    typedef std::map<Tx, DataPair> OrderPool;
    typedef typename OrderPool::value_type OrderPoolData;

 public:
    void set_target_y(Ty new_target_y) {
      target_y = new_target_y;
    }

    void add_pair(Tx new_x, Ty new_y)
    {
        DataPair new_item;
        new_item.x = new_x;
        new_item.y = new_y;

        if (data_pool.size() >= TMaxSample) {
            order_pool.erase(data_pool.front());
            data_pool.pop_front();
        }

        if (order_pool.find(new_x) != order_pool.end()) {
            auto iter = std::find(data_pool.begin(),
                                  data_pool.end(),
                                  new_x);
            data_pool.erase(iter);
        }
        data_pool.push_back(new_x);
        order_pool[new_x] = new_item;
    }

    Tx estimate_x()
    {
        const DataPair* closest[] = {NULL, NULL};
        long  min_delta[] = {LONG_MAX, LONG_MIN};
        long  delta;
        double x[2], y[2];
        DataPair* last_found_point = NULL;

        for(auto& each : order_pool) {
            delta = (long)(each.second.y - target_y);
            if (delta >= 0) {
                if (delta <= min_delta[0]) {
                    closest[0] = &each.second;
                    min_delta[0] = delta;
                    last_found_point = &each.second;
                }
            } else if (delta >= min_delta[1]) {
                closest[1] = &each.second;
                min_delta[1] = delta;
                last_found_point = &each.second;
            }
        }

        /*
         * we have no 2 nearest points in both side of target_y
         * so we fallback to pure liner case.
         */
        if (!closest[0] || !closest[1]) {
            if (last_found_point) {
                return pure_liner_x(last_found_point->x,
                                    last_found_point->y);
            } else  {
                fprintf(stderr,
                        "WARNING: No enough points to estimate x, using fail default %f\n",
                        fail_default);
                return fail_default;
            }
        }

        for (size_t i = 0; i < sizeof(x)/sizeof(x[0]); ++i) {
            x[i] = (double)closest[i]->x;
            y[i] = (double)closest[i]->y;
        }

        factor_a = (y[1] - y[0]) / (x[1] - x[0]);
        factor_b = (x[0] * y[1] - x[1] * y[0]) / (x[0] - x[1]);

        return (target_y - factor_b) / factor_a;
    }

    /*
      same as the old GlobalScan::update_interval()
     */
    Tx pure_liner_x(Tx old_x, Ty new_y)
    {
        Tx est_x;
        float ratio;

        ratio = (float)target_y / ((float)new_y + 1.0);
        if (ratio > 10)
          ratio = 10;
        else if (ratio < 0.2)
          ratio = 0.2;

        est_x = old_x * ratio;
        if (est_x < 0.000001)
            est_x = 0.000001;
        if (est_x > 100)
            est_x = 100;

        return est_x;
    }

    OrderPool order_pool;
    std::list<Tx> data_pool;

    // y = ax + b
    float factor_a;
    float factor_b;
    const float fail_default = 0.01f;

    Ty  target_y;
};
