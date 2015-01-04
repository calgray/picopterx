/**
 * @file libgpsmm.h
 * @brief Emulation of libgpsmm
 */

#ifndef _LIBGPSMM_STUB_H
#define _LIBGPSMM_STUB_H

#include <thread>
#include <chrono>
#include <cstdlib>

#define DEFAULT_GPSD_PORT ""
#define WATCH_ENABLE 1
#define WATCH_JSON 1

#define LATLON_SET 1

struct gps_data_t {
    double time;
    struct fix {
        double latitude;
        double epy;
        double longitude;
        double epx;
    } fix;
    int set;
};

class gpsmm {
    public:
        gpsmm(const char *host, const char *port) : m_dat{0,{},LATLON_SET} {
            srand(time(NULL));
        };
        virtual ~gpsmm() {};
        void *stream(int a) { return &m_dat;};
        bool waiting(int timeout) { 
            std::this_thread::sleep_for(std::chrono::microseconds(timeout));
            m_dat.set ^= rand() % 2; 
            return true;
        };
        struct gps_data_t * read() {return &m_dat;};
    private:
        struct gps_data_t m_dat;
};

#endif // _LIBGPSMM_STUB_H