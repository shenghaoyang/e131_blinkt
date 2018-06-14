/**
 * \file deleters.cpp
 *
 * Deleter classes meant for use with std::unique_ptr and
 * std::shared_ptr, used to release pointer resources.
 *
 * \copyright Shenghao Yang, 2018
 *
 * See LICENSE for details
 */
#include <deleters.hpp>

namespace deleters
{

void
sd_event::operator()(::sd_event* p)
{
  sd_event_unref(p);
};

void
sd_event_source::operator()(::sd_event_source* evs)
{
  sd_event_source_unref(evs);
}

} // namespace deleters
