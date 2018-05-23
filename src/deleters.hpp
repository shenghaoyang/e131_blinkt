/**
 * \file deleters.hpp
 *
 * Deleter class declarations.
 *
 * \sa deleters.cpp
 *
 * \copyright Shenghao Yang, 2018
 * 
 * See LICENSE for details
 */

#ifndef DELETERS_HPP_
#define DELETERS_HPP_

#include <systemd/sd-event.h>

namespace deleters {

struct sd_event {
    void operator()(::sd_event* ev);
};

struct sd_event_source {
    void operator()(::sd_event_source* evs);
};

}

#endif /* DELETERS_HPP_ */
