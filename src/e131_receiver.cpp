/**
 * \file e131_receiver.cpp
 *
 * \copyright Shenghao Yang, 2018
 * 
 * See LICENSE for details
 */

#include <e131_receiver.hpp>

namespace e131_receiver {

std::string cid_str(const cid& uuid) {
    std::string s;
    static constexpr std::array<char, 16> hex_lut {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
        'a', 'b', 'c', 'd', 'e', 'f'
    };

    s.reserve(34);
    s.append("0x");

    for (const auto b : uuid) {
        std::uint8_t msnibble { static_cast<std::uint8_t>((b & 0xf0) >> 0x04) };
        std::uint8_t lsnibble { static_cast<std::uint8_t>(b & 0x0f) };
        s.push_back(hex_lut[msnibble]);
        s.push_back(hex_lut[lsnibble]);
    }

    return s;
}

unique_fd::unique_fd() {
}

unique_fd::unique_fd(int f)
    : fd { f } {
}

unique_fd::operator int() {
    return fd;
}

unique_fd::~unique_fd() {
    if (fd != -1)
        close(fd);
}


priority::priority() {
    prio_cnt[minimum_priority] = 1;
}

priority::operator priority::priority_type() const {
    return prio_cnt.rbegin()->first;
}

priority::priority_type priority::add(priority_type p) {
    ++prio_cnt[p];
    return *this;
}

priority::priority_type priority::remove(priority_type p) {
    --prio_cnt[p];
    if (prio_cnt[p] <= 0)
        prio_cnt.erase(p);
    return *this;
}

priority::count_type priority::sources() const {
    return (*this == minimum_priority)
        ? prio_cnt.rbegin()->second - 1
        : prio_cnt.rbegin()->second;
}

priority::count_type priority::total_sources() const {
    return (std::accumulate(prio_cnt.cbegin(), prio_cnt.cend(), count_type { },
        [](const count_type& count, const auto& pair) {
        return (count + pair.second); }) - 1);
}

channel_data_updated_event::channel_data_updated_event(const cid& uuid)
    : update_event { update_event::event_type::CHANNEL_DATA_UPDATED, uuid } {
}

source_added_event::source_added_event(const cid& uuid)
    : update_event { update_event::event_type::SOURCE_ADDED, uuid } {
}

source_removed_event::source_removed_event(const cid& uuid)
    : update_event { update_event::event_type::SOURCE_REMOVED, uuid } {
}

source_limit_reached_event::source_limit_reached_event(const cid& uuid)
    : update_event { update_event::event_type::SOURCE_LIMIT_REACHED, uuid }
    {
}

void universe::add_source(const cid& uuid, const e131_packet_t& pkt) {
    if (srcs.size() == max_sources)
        throw source_limit_reached_event { uuid };

    int r;
    std::uint64_t now;
    if ((r = sd_event_now(ev.get(), CLOCK_MONOTONIC, &now)) < 0)
        throw std::system_error { -r, std::system_category() };

    sd_event_source* evs;
    if ((r = sd_event_add_time(ev.get(), &evs, CLOCK_MONOTONIC,
            now + (static_cast<std::uint64_t>(network_data_loss_timeout)
            * UINT64_C(1000)), 0, timer_callback, this)) < 0)
        throw std::system_error { -r, std::system_category() };

    prio.add(pkt.frame.priority);
    srcs.try_emplace(uuid,
        source {uuid, pkt.frame.priority, pkt.frame.seq_number, 0,
        std::unique_ptr<sd_event_source, deleters::sd_event_source> {evs} }
    );
    evs_cid.try_emplace(evs, uuid);
    queued_events.push_back(source_added_event { uuid });
}

void universe::source_timer_reset(const source& src) {
    int r;
    std::uint64_t now;
    if ((r = sd_event_now(ev.get(), CLOCK_MONOTONIC, &now)) < 0)
        throw std::system_error { -r, std::system_category() };

    if ((r = sd_event_source_set_time(src.timer_evs.get(),
        now + (static_cast<std::uint64_t>(network_data_loss_timeout)
        * UINT64_C(1000)))) < 0)
        throw std::system_error { -r, std::system_category() };
}

void universe::remove_source(const source& src) {
    cid uuid { src.uuid };

    prio.remove(src.prio);
    evs_cid.erase(src.timer_evs.get());
    srcs.erase(src.uuid);

    queued_events.push_back(source_removed_event { uuid });
}

bool universe::valid_packet(e131_packet_t& pkt) {
    if ((e131_pkt_validate(&pkt) == E131_ERR_NONE)
        && (be32toh(pkt.root.vector) == e131_data_vector)
        && (be16toh(pkt.frame.universe) == uni)
        && ((!e131_get_option(&pkt, E131_OPT_PREVIEW)
            || ignore_preview_flag)))
        return true;

    return false;
}

int universe::timer_callback(sd_event_source* const s,
    std::uint64_t usec, void* userdata) {
    universe& uni { *reinterpret_cast<universe* const>(userdata) };
    source& src { uni.srcs[uni.evs_cid.at(s)] };

    try {
        uni.remove_source(src);
    } catch (const std::exception& e) {
        sd_event_exit(uni.ev.get(), -1);
        return -1;
    }
    return 0;
}

bool universe::socket_handler(std::uint32_t revents) {
    try {
        if (revents & EPOLLERR)
            throw std::runtime_error { "error on E1.31 socket" };
        /* Other EPOLL events don't happen for UDP sockets */
        do {
            e131_packet_t pkt;
            ::ssize_t r { e131_recv(e131_socket, &pkt) };
            if (r == -1) {
                if ((errno != EWOULDBLOCK) && (errno != EAGAIN))
                    throw std::system_error { errno,
                        std::system_category() };
                break;
            }

            if (!valid_packet(pkt))
                break;

            cid uuid { pkt.root.cid, pkt.root.cid + sizeof(pkt.root.cid) };
            bool terminated { e131_get_option(&pkt, E131_OPT_TERMINATED) };
            bool registered_source { srcs.find(uuid) != srcs.end() };

            if (registered_source) {
                source& src { srcs[uuid] };

                if (e131_pkt_discard(&pkt, src.sequence_data))
                    break;

                if (terminated)
                    remove_source(src);
                if (pkt.frame.priority != src.prio) {
                    prio.remove(src.prio);
                    src.prio = prio.add(pkt.frame.priority);
                }
            } else if (terminated) {
                break;
            } else {
                add_source(uuid, pkt);
            }

            source& src { srcs[uuid] };
            source_timer_reset(src);

            if ((pkt.frame.priority >= prio)
                && pkt.dmp.prop_val_cnt && (pkt.dmp.prop_val[0] == 0x00)) {
                std::copy(pkt.dmp.prop_val + 1, pkt.dmp.prop_val
                    + be16toh(pkt.dmp.prop_val_cnt), channel_data.data());
                queued_events.push_back(channel_data_updated_event { uuid });
            }

            src.sequence_data = pkt.frame.seq_number;
        } while (true);
    } catch (const std::exception& e) {
        return false;
    }

    return true;
}

int universe::socket_callback(sd_event_source* s, int fd,
    std::uint32_t revents, void* userdata) {
    universe& uni { *reinterpret_cast<universe* const>(userdata) };

    if (!uni.socket_handler(revents)) {
        sd_event_exit(uni.ev.get(), -1);
        return -1;
    }
    return 0;
}

universe::universe(priority::count_type sources, bool preview_flag_ignore,
    int universe_num)
: max_sources { sources }, ignore_preview_flag { preview_flag_ignore },
  uni { universe_num }, e131_socket { ::e131_socket() } {
      int r;
      sd_event* evp;

      if ((e131_socket == -1)
          || (::e131_bind(e131_socket, E131_DEFAULT_PORT) == -1))
          throw std::system_error { errno, std::system_category() };

      int flags { fcntl(e131_socket, F_GETFL) };
      if (flags == -1)
          throw std::system_error { errno, std::system_category() };

      if (fcntl(e131_socket, F_SETFL, flags | O_NONBLOCK))
          throw std::system_error { errno, std::system_category() };

      if ((r = sd_event_new(&evp)) < 0)
          throw std::system_error { -r, std::system_category() };

      ev.reset(evp);

      if ((r = sd_event_add_io(ev.get(), nullptr, e131_socket,
              EPOLLIN | EPOLLERR, socket_callback, this)) < 0)
          throw std::system_error { -r, std::system_category() };
}

int universe::event_fd() const {
    return sd_event_get_fd(ev.get());
}

const std::vector<update_event>& universe::update() {
    int r;
    queued_events.clear();

    while ((r = sd_event_run(ev.get(), 0)) > 0);

    if (r < 0)
        throw std::system_error { -r, std::system_category() };
    return queued_events;
}

const priority& universe::prio_tracker() const {
    return prio;
}

const universe::channel_data_type& universe::dmx_data() const {
    return channel_data;
}
}
