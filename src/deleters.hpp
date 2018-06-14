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

/**
 * Deleters for \code sd-event loop objects.
 *
 * Meant for use with \ref std::unique_ptr and \ref std::shared_ptr
 */
namespace deleters
{

/**
 * Deleter for resource pointed to by a \code sd_event*
 */
struct sd_event {
  void
  operator()(::sd_event* ev);
};

/**
 * Deleter for resource pointed to by a \code sd_event_source*
 */
struct sd_event_source {
  void
  operator()(::sd_event_source* evs);
};

} // namespace deleters

#endif /* DELETERS_HPP_ */
