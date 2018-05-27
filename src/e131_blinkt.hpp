/**
 * \file e131_blinkt.hpp
 *
 * \copyright Shenghao Yang, 2018
 * 
 * See LICENSE for details
 */

#ifndef E131_BLINKT_HPP_
#define E131_BLINKT_HPP_

#include <e131_receiver.hpp>
#include <deleters.hpp>
#include <docopt/docopt.h>
#include <apa102.hpp>
#include <libconfig.h++>
#include <systemd/sd-journal.h>
#include <systemd/sd-event.h>
#include <systemd/sd-daemon.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <system_error>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <limits>
#include <map>

/**
 * Utility functionality for e131_blinkt.
 */
namespace e131_blinkt {
/**
 * Structure representing configuration settings for the e131_blinkt daemon.
 */
struct config_settings {
    /**
     * Blinkt-device specific configuration.
     */
    struct {
        std::string path;   ///< Path to GPIO character device for Blinkt.
        int clock;          ///< GPIO character device line for clock signal.
        int data;           ///< GPIO character device line for data signal.
    } blinkt;

    /**
     * E1.31 specific configuration.
     */
    struct {
        static_assert(std::numeric_limits<int>::max() >=
            std::numeric_limits<std::uint16_t>::max(),
            "int type not large enough to hold DMX universe number");
        int universe;               ///< Universe to listen on
        int max_sources;            ///< Maximum number of sources
        int offset;                 ///< Pixel data channel number offset.
        bool ignore_preview_flag;   ///< Preview flag ignore.
    } e131;

    /* This simply contains base data types, so... */
    config_settings() = default;
    /* Allow implicit default move constructor */
    /* Allow implicit default copy constructor */
    /* Allow implicit default copy-assign operator */
    /* Allow implicit default move-assign operator */
    /**
     * Initialize structure from a configuration object.
     *
     * \param conf configuration object, that has successfully parsed the
     *             e131_blinkt configuration file.
     * \param path path to GPIO character device to use for driving Blinkt.
     * \throws SettingNotFoundException on missing settings.
     */
    explicit config_settings(const libconfig::Config& conf,
        const std::string& path);
};

/**
 * Operator overload to dump configuration settings to an output stream, in a
 * human-readable form.
 *
 * \param ost output stream to dump settings to.
 * \param settings settings object.
 * \param reference to the target output stream.
 */
std::ostream& operator<<(std::ostream& ost, const config_settings& settings);

/**
 * Operator overload to dump command line arguments to an output stream, in a
 * human-readable form.
 *
 * \param ost output stream to command line arguments to.
 * \param m mapping containing the command line arguments.
 * \return reference to target output stream.
 */
std::ostream& operator<<(std::ostream& ost,
    std::map<std::string, docopt::value> m);

/**
 * Context structure passed to the E1.31 socket data ready handler.
 */
#ifndef DEBUG
struct handler_info {
    e131_receiver::universe& uni;              ///< Reference to universe object
    apa102::apa102& blinkt;                    ///< Reference to Blinkt handle
    int channel_offset;                        ///< Pixel data channel offset
};
#else
struct handler_info {
    e131_receiver::universe& uni;
};
#endif
}

#endif /* E131_BLINKT_HPP_ */
