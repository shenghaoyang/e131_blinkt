/**
 * \file docopt_testing.cpp
 *
 * \copyright Shenghao Yang, 2018
 *
 * See LICENSE for details
 */
#include <e131_blinkt.hpp>

static const char* cmd_help {
    R"(e131_blinkt - command a Pimoroni Blinkt! through E1.31

Usage:
    e131_blinkt [--help] [--verbose] [--gpiodev=FILE] [--config=FILE]
    
Options:
    --help          display this help messagel
    --verbose       enable verbose output for debugging
    --gpiodev=FILE  path to gpio character device [default: /dev/gpiochip0]
    --config=FILE   config file  [default: /etc/e131_blinkt/e131_blinkt.conf]
)" };

static int sigterm_handler(sd_event_source* s, const struct signalfd_siginfo *si,
    void* userdata) {
    sd_event* evloop = sd_event_source_get_event(s);
    sd_event_exit(evloop, EXIT_SUCCESS);
    return 0;
}

static int universe_handler(sd_event_source* s, int fd, uint32_t revents,
    void* userdata) {
    using namespace e131_blinkt;
    using uni_type = e131_receiver::universe;
    using event_type = e131_receiver::update_event::event_type;
    auto& info { *reinterpret_cast<handler_info* const>(userdata) };
    uni_type& uni { info.uni };
#ifndef DEBUG
    auto& blinkt { info.blinkt };
#endif
    sd_event* ev_loop { sd_event_source_get_event(s) };

    try {
        if (!ev_loop)
            throw std::runtime_error { "Error obtaining pointer to parent "
                "sd_event from a sd_event_source referenced to by a pointer" };

        if (revents & EPOLLERR) {
            sd_journal_print(LOG_CRIT, "Error event on E1.31 socket");
            throw std::runtime_error { "Error event on E1.31 socket" };
        }
        const auto& events { uni.update() };
        for (const auto& event : events) {
            switch (event.event) {
                case event_type::CHANNEL_DATA_UPDATED: {
#ifndef DEBUG
                    bool updated { false };
                    const auto& channel_data { uni.dmx_data() };
                    for (int i = info.channel_offset;
                         i < blinkt.size(); i++) {
                        apa102::output target {
                            0x1f,
                            channel_data[i * 3],
                            channel_data[(i * 3) + 1],
                            channel_data[(i * 3) + 2]
                        };
                        if (target != blinkt[i]) {
                            blinkt[i] = target;
                            updated = true;
                        }
                    }
                    if (updated)
                        blinkt.commit();
#else
                    std::cerr << "DMX data updated" << std::endl;
#endif
                }
                    break;
                case event_type::SOURCE_ADDED:
                    sd_journal_print(LOG_INFO, "Source %s added to universe. "
                        "(Current max priority: %d [%d sources])",
                        e131_receiver::cid_str(event.id).c_str(),
                        uni.max_priority(), uni.max_priority_sources());
                    break;
                case event_type::SOURCE_REMOVED:
                    sd_journal_print(LOG_INFO, "Source %s "
                        "removed from universe (transmission terminated)."
                        " (Current max priority: %d [%d sources])",
                        e131_receiver::cid_str(event.id).c_str(),
                        uni.max_priority(), uni.max_priority_sources());
                    break;
                case event_type::SOURCE_LIMIT_REACHED:
                    sd_journal_print(LOG_INFO, "Source %s "
                        "not added to universe: source limit reached",
                        e131_receiver::cid_str(event.id).c_str());
                    break;
            }
        }
    } catch (const std::exception& e) {
        sd_journal_print(LOG_CRIT, "Exception processing data from E1.31 "
            "socket: %s", e.what());
        sd_event_exit(ev_loop, EXIT_FAILURE);
    }

    return 0;
}

int main(int argc, char** argv) {
    using namespace e131_blinkt;
    using namespace e131_receiver;

    try {
        std::map<std::string, docopt::value> arguments { docopt::docopt(
            cmd_help, { argv + 1, argv + argc }, true, "1.0.0") };
        if (arguments["--verbose"].asBool())
            std::cout << arguments;

        libconfig::Config config { };
        config_settings user_settings { };
        config.readFile(arguments["--config"].asString().c_str());
        user_settings = config_settings { config,
            arguments["--gpiodev"].asString() };
        if (arguments["--verbose"].asBool())
            std::cout << user_settings;

        /*
         * Block SIGTERM to add them to the event loop
         */
        sigset_t set;
        if (sigemptyset(&set)) {
            sd_journal_print(LOG_CRIT, "Unable to clear signal set: %s",
                strerror(errno));
            throw std::system_error { errno, std::system_category() };
        }

        if (sigaddset(&set, SIGTERM)) {
            sd_journal_print(LOG_CRIT,
                "Unable to add SIGTERM to signal set: %s", strerror(errno));
            throw std::system_error { errno, std::system_category() };
        }
        if (sigprocmask(SIG_BLOCK, &set, nullptr)) {
            sd_journal_print(LOG_CRIT, "Unable to block SIGTERM and SIGHUP: %s",
                strerror(errno));
            throw std::system_error { errno, std::system_category() };
        }

        int r;
        sd_event* temp;
        std::unique_ptr<sd_event, deleters::sd_event> ev_loop;

        if ((r = sd_event_new(&temp)) < 0) {
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

        e131_receiver::universe uni {
            user_settings.e131.max_sources,
            user_settings.e131.ignore_preview_flag, user_settings.e131.universe
        };
#ifndef DEBUG
        apa102::apa102 blinkt { user_settings.blinkt.path, 8,
            static_cast<std::uint8_t>(user_settings.blinkt.clock),
            static_cast<std::uint8_t>(user_settings.blinkt.data), true };
        handler_info info { uni, blinkt, user_settings.e131.offset };
#else
        handler_info info { uni };
#endif

        if ((r = sd_event_add_io(ev_loop.get(), nullptr, uni.event_fd(),
            EPOLLIN | EPOLLHUP | EPOLLERR, universe_handler, &info)) < 0) {
            sd_journal_print(LOG_CRIT,
                "Unable to add E1.31 universe object to event loop: "
                    "%s", strerror(-r));
            throw std::system_error { -r, std::system_category() };
        }

        sd_notify(0, "READY=1\nSTATUS=Awaiting data sources.");

        sd_journal_print(LOG_INFO, "listening for DMX data addressed to "
            "universe %d", user_settings.e131.universe);

        if ((r = sd_event_loop(ev_loop.get())) < 0) {
            sd_journal_print(LOG_CRIT, "Error running the event loop: %s",
                strerror(-r));
            throw std::system_error { -r, std::system_category() };
        }

    } catch (const std::exception& e) {
        sd_journal_print(LOG_CRIT, "Error running daemon: %s", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
