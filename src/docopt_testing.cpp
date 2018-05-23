/**
 * \file docopt_testing.cpp
 *
 * \copyright Shenghao Yang, 2018
 *
 * See LICENSE for details
 */
#include <deleters.hpp>
#include <e131_receiver.hpp>
#include <libconfig.h++>
#include <docopt/docopt.h>
#include <systemd/sd-journal.h>
#include <systemd/sd-event.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-id128.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <map>
#include <sstream>
#include <cstdlib>
#include <numeric>
#include <cstdint>
#include <memory>
#include <system_error>
#include <algorithm>

static const char* cmd_help {
    R"(e131_blinkt - command a Pimoroni Blinkt! through E1.31

Usage:
    e131_blinkt [--help] [--verbose] [--config=FILE]
    
Options:
    --help          display this help messagel
    --verbose       enable verbose output for debugging
    --config=FILE   config file  [default: /etc/e131_blinkt/e131_blinkt.conf]
)" };

struct config_settings {
    struct {
        std::string path;
        int clock;
        int data;
    } blinkt;
    struct {
        static_assert(std::numeric_limits<int>::max() >=
            std::numeric_limits<std::uint16_t>::max(),
            "int type not large enough to hold DMX universe number");
        int universe;
        int max_sources;
        int offset;
        bool ignore_preview_flag;
    } e131;

    /* This simply contains base data types, so... */
    config_settings() = default;
    /* Allow implicit default move constructor */
    /* Allow implicit default copy constructor */
    /* Allow implicit default copy-assign operator */
    /* Allow implicit default move-assign operator */
    /**
     * Initialize from a configuration object.
     *
     * \param conf configuration object, that has successfully parsed the
     *             e131_blinkt configuration file.
     * \throws SettingNotFoundException on missing settings.
     */
    explicit config_settings(const libconfig::Config& conf)
        : blinkt {
            conf.lookup("e131_blinkt.blinkt.path").operator std::string(),
            conf.lookup("e131_blinkt.blinkt.clock"), conf.lookup(
                "e131_blinkt.blinkt.data") }, e131 { conf.lookup(
            "e131_blinkt.e131.universe"), conf.lookup(
            "e131_blinkt.e131.max_sources"), conf.lookup(
            "e131_blinkt.e131.offset"), conf.lookup(
            "e131_blinkt.e131.ignore_preview_flag") } {
    }
};

std::ostream& operator<<(std::ostream& ost, const config_settings& settings) {
    ost << "Configuration settings:" << std::endl;
    ost << "Blinkt settings:" << std::endl;
    ost << "\tgpiochip: " << settings.blinkt.path << std::endl;
    ost << "\tclock line: " << settings.blinkt.clock << std::endl;
    ost << "\tdata line: " << settings.blinkt.data << std::endl;

    ost << "E1.31 settings:" << std::endl;
    ost << "\tUniverse: " << settings.e131.universe << std::endl;
    ost << "\tMax sources: " << settings.e131.max_sources << std::endl;
    ost << "\tDMX channel offset: " << settings.e131.offset << std::endl;
    ost << "\tPreview flag ignored: " << settings.e131.ignore_preview_flag
        << std::endl;
    return ost;
}

/**
 * SIGTERM handler, causes the event loop to quit, so that the exit
 * steps are taken.
 *
 * \see sd_event_add_signal for more information.
 */
int sigterm_handler(sd_event_source* s, const struct signalfd_siginfo *si,
    void* userdata) {
    sd_event* evloop = sd_event_source_get_event(s);
    sd_event_exit(evloop, EXIT_SUCCESS);
    return 0;
}

/**
 * Stub SIGHUP handler for now, simply signals that we are doing a reload.
 *
 * \see sd_event_add_signal for more information.
 */
int sighup_handler(sd_event_source* s, const struct signalfd_siginfo *si,
    void* userdata) {
    sd_notify(0, "RELOADING=1");
    sd_notify(0, "READY=1");
    return 0;
}

int socket_handler(sd_event_source* s, int fd, uint32_t revents,
    void* userdata) {
    auto& uni { *reinterpret_cast<e131_receiver::universe<
        e131_receiver::systemd_event_timer>* const >(userdata) };
    using uni_type
    = e131_receiver::universe<e131_receiver::systemd_event_timer>;
    sd_event* ev_loop = sd_event_source_get_event(s);
    int r;
    uint64_t now;
    if (!ev_loop)
        goto event_exit_fail;

    if ((r = sd_event_now(ev_loop, CLOCK_MONOTONIC, &now)) < 0) {
        sd_journal_print(LOG_CRIT, "Unable to obtain current time: %s",
            strerror(-r));
        goto event_exit_fail;
    }
    if (revents & EPOLLERR) {
        sd_journal_print(LOG_CRIT, "Error event on E1.31 socket");
        goto event_exit_fail;
    }

    if (revents & EPOLLIN) {
        e131_packet_t p;
        if (e131_recv(fd, &p) < 0) {
            sd_journal_print(LOG_CRIT, "Error reading from E1.31 socket: %s",
                strerror(errno));
            goto event_exit_fail;
        }
        uni_type::update_return ev { };
        do {
            ev = uni.update(p);
            switch (ev->event) {
                case uni_type::update_event::FATAL_ERROR: {
                    auto error { dynamic_cast<uni_type::fatal_error_event&>(*ev) };
                    sd_journal_print(LOG_CRIT,
                        "Error updating E1.31 universe: %s",
                        error.what().c_str());
                    goto event_exit_fail;
                }
                case uni_type::update_event::SOURCE_ADDED: {
                    auto event {
                        dynamic_cast<uni_type::source_added_event&>(*ev) };
                    std::ostringstream oss;

                    std::for_each(ev->id.cbegin(), ev->id.cend(),
                        [&oss](std::uint32_t v) {
                            oss << std::hex << v << std::dec;
                        });
                    sd_journal_print(LOG_INFO, "Source %s "
                        "added to universe. "
                        " (Current max priority: %d [%d sources])",
                        oss.str().c_str(), uni.max_priority(),
                        uni.max_priority_sources());
                }
                    break;
                case uni_type::update_event::SOURCE_REMOVED: {
                    auto event {
                        dynamic_cast<uni_type::source_removed_event&>(*ev) };
                    std::ostringstream oss;

                    std::for_each(ev->id.cbegin(), ev->id.cend(),
                        [&oss](std::uint32_t v) {
                            oss << std::hex << v << std::dec;
                        });
                    sd_journal_print(LOG_INFO, "Source %s "
                        "removed from universe (transmission terminated)."
                        " (Current max priority: %d [%d sources])",
                        oss.str().c_str(), uni.max_priority(),
                        uni.max_priority_sources());
                }
                    break;
                case uni_type::update_event::SOURCE_LIMIT_REACHED: {
                    auto event {
                        dynamic_cast<uni_type::source_limit_reached_event&>(*ev) };
                    std::ostringstream oss;

                    std::for_each(ev->id.cbegin(), ev->id.cend(),
                        [&oss](std::uint32_t v) {
                            oss << std::hex << v << std::dec;
                        });
                    sd_journal_print(LOG_INFO, "Source %s "
                        "not added to universe: source limit reached",
                        oss.str().c_str());
                }
                    break;
                case uni_type::update_event::NONE:
                    break;
            }
        } while (ev->event != uni_type::update_event::NONE);
    }
    std::cout << "DMX Channel data:" << std::endl;
    for (int i = 0; i < 32; i++) {
        std::cout << static_cast<int>(uni.dmx_data()[i]) << " ";
    }
    std::cout << std::endl;
    return 0;

    event_exit_fail: sd_event_exit(ev_loop, EXIT_FAILURE);
    return 0;
}

class unique_fd {
private:
    int fd;
public:
    explicit unique_fd()
        : fd { -1 } {

    }
    explicit unique_fd(int f)
        : fd { f } {

    }
    unique_fd(const unique_fd& other) = delete;
    unique_fd(const unique_fd&& other) = delete;
    unique_fd& operator=(const unique_fd& other) = delete;
    operator int() {
        return fd;
    }
    ~unique_fd() {
        if (fd != -1)
            close(fd);
    }
};



int main(int argc, char** argv) {
    try {
        std::map<std::string, docopt::value> arguments { docopt::docopt(
            cmd_help, { argv + 1, argv + argc }, true, "1.0.0") };
        if (arguments["--verbose"].asBool()) {
            std::cout << "e131_blinkt called with options:" << std::endl;
            for (const auto& arg : arguments) {
                std::cout << std::get<0>(arg) << " : " << std::get<1>(arg)
                    << std::endl;
            }
        }

        libconfig::Config config { };
        config_settings user_settings { };
        try {
            // std::string reference returned by asString() points to a
            // string stored internally in the value object, so no
            // usage of temporary values is present.
            config.readFile(arguments["--config"].asString().c_str());
            user_settings = config_settings { config };
            if (arguments["--verbose"].asBool())
                std::cout << user_settings;
        } catch (const libconfig::ConfigException& e) {
            std::cerr << "Exception when loading & parsing configuration: "
                << e.what() << std::endl;
            throw;
        }

        /* Initialize the daemon */
        int r = 0;

        /*
         * Block SIGTERM and SIGHUP in order to add those signals to the
         * sd-event event loop.
         */
        sigset_t set;
        if ((r = sigemptyset(&set))) {
            sd_journal_print(LOG_CRIT, "Unable to clear signal set: %s",
                strerror(errno));
            throw std::system_error { errno, std::system_category() };
        }
        if ((r = sigaddset(&set, SIGTERM))) {
            sd_journal_print(LOG_CRIT,
                "Unable to add SIGTERM to signal set: %s", strerror(errno));
            throw std::system_error { errno, std::system_category() };
        }
        if ((r = sigaddset(&set, SIGHUP))) {
            sd_journal_print(LOG_CRIT, "Unable to add SIGHUP to signal set: %s",
                strerror(errno));
            throw std::system_error { errno, std::system_category() };
        }
        if ((r = sigprocmask(SIG_BLOCK, &set, nullptr))) {
            sd_journal_print(LOG_CRIT, "Unable to block SIGTERM and SIGHUP: %s",
                strerror(errno));
            throw std::system_error { errno, std::system_category() };
        }

        /*
         * Setup the socket to listen to E131 messages
         */
        if ((r = e131_socket()) == -1) {
            sd_journal_print(LOG_CRIT, "Unable to open socket to listen"
                " for E131 messages: %s", strerror(errno));
            throw std::system_error { errno, std::system_category() };
        }

        unique_fd sock { r };

        if ((r = e131_bind(sock, E131_DEFAULT_PORT)) == -1) {
            sd_journal_print(LOG_CRIT, "Unable to bind listening socket: %s",
                strerror(errno));
            throw std::system_error { errno, std::system_category() };
        }
        int flags;
        if ((r = flags = fcntl(sock, F_GETFL)) == -1) {
            sd_journal_print(LOG_CRIT, "Unable to set get flags on "
                "socket: %s", strerror(errno));
            throw std::system_error { errno, std::system_category() };
        }
        if ((r = fcntl(sock, F_SETFL, flags | O_NONBLOCK))) {
            sd_journal_print(LOG_CRIT, "Unable to setup non-blocking flag on "
                "socket: %s", strerror(errno));
            throw std::system_error { errno, std::system_category() };
        }

        /*
         * Setup the event loop
         */
        uint64_t now;
        sd_event* temp;
        std::unique_ptr<sd_event, deleters::sd_event> ev_loop;

        if ((r = sd_event_default(&temp)) < 0) {
            sd_journal_print(LOG_CRIT, "Unable to allocate event loop: %s",
                strerror(-r));
            throw std::system_error { -r, std::system_category() };
        }

        ev_loop.reset(temp);

        if ((r = sd_event_add_signal(ev_loop.get(), nullptr, SIGTERM,
            sigterm_handler, nullptr)) < 0) {
            sd_journal_print(LOG_CRIT, "Unable to add SIGTERM to event loop:"
                "%s", strerror(-r));
            throw std::system_error { -r, std::system_category() };
        }
        if ((r = sd_event_add_signal(ev_loop.get(), nullptr, SIGHUP,
            sighup_handler, nullptr)) < 0) {
            sd_journal_print(LOG_CRIT, "Unable to add SIGHUP to event loop:"
                "%s", strerror(-r));
            throw std::system_error { -r, std::system_category() };
        }
        if ((r = sd_event_now(ev_loop.get(), CLOCK_MONOTONIC, &now) < 0)) {
            sd_journal_print(LOG_CRIT, "Unable to obtain current time: %s",
                strerror(-r));
            throw std::system_error { -r, std::system_category() };
        }

        e131_receiver::universe<e131_receiver::systemd_event_timer> uni {
            user_settings.e131.max_sources, ev_loop.get(),
            user_settings.e131.ignore_preview_flag, user_settings.e131.universe };

        if ((r = sd_event_add_io(ev_loop.get(), nullptr, sock,
        EPOLLIN, socket_handler, &uni)) < 0) {
            sd_journal_print(LOG_CRIT,
                "Unable to add E1.31 socket to event loop: "
                    "%s", strerror(-r));
            throw std::system_error { -r, std::system_category() };
        }

        sd_notify(0, "READY=1\nSTATUS=Awaiting E1.31 data sources.");
        sd_journal_print(LOG_INFO, "E1.31 receiver node listening on all "
            "addresses, watching for DMX data addressed to universe %d",
            user_settings.e131.universe);

        if ((r = sd_event_loop(ev_loop.get())) < 0) {
            sd_journal_print(LOG_CRIT, "Error running the event loop: %s",
                strerror(-r));
            throw std::system_error { -r, std::system_category() };
        }

    } catch (const std::exception& e) {
        sd_journal_print(LOG_CRIT, "Unexpected exception: %s", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
