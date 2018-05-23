/**
 * \file e131_receiver.hpp
 *
 * \copyright Shenghao Yang, 2018
 * 
 * See LICENSE for details
 */

#ifndef E131_RECEIVER_HPP_
#define E131_RECEIVER_HPP_

#include <deleters.hpp>
#include <e131.h>
#include <systemd/sd-event.h>
#include <endian.h>
#include <map>
#include <stdexcept>
#include <array>
#include <iterator>
#include <queue>
#include <cassert>
#include <string>
#include <cstdint>
#include <memory>

namespace e131_receiver {
/**
 * Type used to represent the 128 bit UUID of the source.
 * Stored big endian.
 */
using cid = std::basic_string<std::uint8_t>;

/**
 * E1.31 Network data loss timeout, in milliseconds.
 */
constexpr std::uint32_t network_data_loss_timeout { 2500 };

/**
 * E1.31 Root Layer Protocol Vector representing a payload of E1.31 data.
 */
constexpr std::uint32_t e131_data_vector { 0x00000004 };

/**
 * Used to represent the current priority level for a E1.31 universe
 * (not official E1.31 terminology). The priority level here refers to the
 * minimum priority E1.31 sources must be transmitting at before their data
 * can be output.
 */
class priority {
public:
    /**
     * Underlying priority type
     */
    using priority_type = std::uint8_t;
    /**
     * Underlying source count type
     */
    using count_type = int;
    /**
     * Default E1.31 priority
     */
    static constexpr uint8_t default_priority { 100 };

private:
    std::map<priority_type, count_type> prio_cnt { };

public:
    /* Allow default move constructor */
    /* Allow default copy constructor */
    /* Allow default copy-assign */
    /* Allow default move-assign */

    /**
     * Default constructor.
     *
     * Sets priority to the default E1.31 priority and source count to zero.
     */
    priority() {
        prio_cnt[default_priority] = 1;
    }

    operator priority_type() const {
        return prio_cnt.rbegin()->first;
    }

    priority_type add(priority_type p) {
        ++prio_cnt[p];
        return *this;
    }

    priority_type remove(priority_type p) {
        --prio_cnt[p];
        if (prio_cnt[p] <= 0)
            prio_cnt.erase(p);
        return *this;
    }

    count_type sources() const {
        return (*this == default_priority)
            ? prio_cnt.rbegin()->second - 1
            : prio_cnt.rbegin()->second;
    }
};

/**
 * Structure representing information regarding a particular source of
 * E1.31 DMX data.
 */
struct source {
    cid uuid;                              ///< Source CID
    priority::priority_type prio;          ///< Priority at which the source broadcasts
    std::uint8_t sequence_data;            ///< Sequence of the last E1.31 data packet
    std::uint8_t sequence_synchronization; ///< Sequence of the last E1.31 sync packet
};

/**
 * Structure representing information regarding a particular E1.31 universe
 */
template<typename timer>
class universe {
public:
    using channel_value_type = std::uint8_t;
    using sources = std::map<cid, source>;

    class update_event {
    public:
        enum event_type {
            NONE,
            SOURCE_ADDED,
            SOURCE_REMOVED,
            SOURCE_LIMIT_REACHED,
            FATAL_ERROR,
        } const event;
        const cid id;
    public:
        update_event(event_type t, const cid& uuid)
        : event { t }, id { uuid } {

        }

        virtual ~update_event() {

        }
    };

    class no_event : public update_event {
    public:
        no_event()
        : update_event { update_event::event_type::NONE, cid { } } {

        }
    };

    class source_added_event : public update_event {
    public:
        source_added_event(const cid& uuid)
        : update_event { update_event::event_type::SOURCE_ADDED, uuid } {

        }
    };

    class source_removed_event : public update_event {
    public:
        source_removed_event(const cid& uuid)
        : update_event { update_event::event_type::SOURCE_REMOVED, uuid } {

        }
    };

    class source_limit_reached_event : public update_event {
    public:
        source_limit_reached_event(const cid& uuid)
        : update_event { update_event::event_type::SOURCE_LIMIT_REACHED, uuid }
        {

        }
    };

    class fatal_error_event : public update_event {
    private:
        const std::string reason;
    public:
        fatal_error_event(const cid& uuid, const std::string& what)
        : update_event { update_event::event_type::FATAL_ERROR, uuid },
          reason { what } {

        }

        const std::string& what() const {
            return reason;
        }
    };
    using update_return = std::unique_ptr<update_event>;
private:
    priority prio { };
    sources srcs { };
    std::array<channel_value_type, 512> channel_data { };
    std::queue<cid> pending_removal { };
    timer tmrs;
    priority::count_type max_sources;
    bool ignore_preview_flag;
    int uni;

    enum class add_source_return {
        ADD_SUCCESS,
        ADD_FAILURE_SOURCE_LIMIT,
        ADD_FAILURE_TIMER,
    };

    add_source_return add_source(const e131_packet_t& pkt) {
        if (srcs.size() == max_sources) {
            return add_source_return::ADD_FAILURE_SOURCE_LIMIT;
        }
        else {
            source src {
                cid { pkt.root.cid, pkt.root.cid + sizeof(pkt.root.cid) },
                pkt.frame.priority,
                pkt.frame.seq_number,
                0,
            };
            if (!tmrs.add(src.uuid, network_data_loss_timeout))
                return add_source_return::ADD_FAILURE_TIMER;
            prio.add(src.prio);
            srcs[src.uuid] = src;
            return add_source_return::ADD_SUCCESS;
        }
    }

    bool remove_source(const cid& id) {
        if (!tmrs.remove(id))
            return false;
        prio.remove(srcs[id].prio);
        srcs.erase(id);
        return true;
    }
public:
    universe(priority::count_type sources, typename timer::key_type k,
        bool preview_flag_ignore, int universe_num)
    : tmrs { k, this }, max_sources { sources },
      ignore_preview_flag { preview_flag_ignore }, uni { universe_num } {

    }
    /* Allow copy constructor */
    /* Allow move constructor */
    /* Allow copy-assign */
    /* Allow move-assign */
    update_return update(const e131_packet_t& pkt) {
        if (!pending_removal.empty()) {
            cid uuid { pending_removal.front() };
            pending_removal.pop();
            if (!remove_source(uuid))
                return update_return {new fatal_error_event {
                    uuid, "Unable to remove source" }
                };
            return update_return { new source_removed_event { uuid } };
        }
        if ((e131_pkt_validate(&pkt) != E131_ERR_NONE)
            && (be32toh(pkt.root.vector) == e131_data_vector)
            && (pkt.frame.universe == uni)
            && ((!e131_get_option(&pkt, E131_OPT_PREVIEW)
                || ignore_preview_flag)))
            return update_return { new no_event { } };

        cid uuid { pkt.root.cid, pkt.root.cid + sizeof(pkt.root.cid) };
        bool terminated { e131_get_option(&pkt, E131_OPT_TERMINATED) };
        bool new_source { false };

        if (srcs.find(uuid) != srcs.end()) {
            source& src { srcs[uuid] };

            if (e131_pkt_discard(&pkt, src.sequence_data))
                return update_return { new no_event { } };
        } else {
            if (terminated)
                return update_return {new no_event { } };

            switch (add_source(pkt)) {
                case add_source_return::ADD_FAILURE_SOURCE_LIMIT:
                    return update_return { new source_limit_reached_event {
                        uuid } };
                case add_source_return::ADD_FAILURE_TIMER:
                    return update_return { new fatal_error_event {
                        uuid, "Failed to add timer" } };
                default:
                    new_source = true;
            }
        }

        // src references a valid source
        source& src { srcs[uuid] };
        if (terminated) {
            if (!tmrs.modify(uuid, 0))
                return update_return { new fatal_error_event {
                    uuid, "Timer modification failure" } };
            return update_return { new no_event { } };
        } else {
            if (!tmrs.modify(uuid, network_data_loss_timeout))
                return update_return { new fatal_error_event {
                    uuid, "Timer modification failure" } };
        }

        if ((pkt.frame.priority >= prio)
            && pkt.dmp.prop_val_cnt
            && (pkt.dmp.prop_val[0] == 0x00))
            std::copy(pkt.dmp.prop_val + 1, pkt.dmp.prop_val
                + be16toh(pkt.dmp.prop_val_cnt), channel_data.data());

        src.sequence_data = pkt.frame.seq_number;

        if (new_source)
            return update_return { new source_added_event { uuid } };
        else
            return update_return { new no_event { } };
    }

    void timer_expired(const cid& uuid) {
        pending_removal.push(uuid);
    }

    priority::priority_type max_priority() const {
        return prio;
    }

    priority::count_type max_priority_sources() const {
        return prio.sources();
    }

    const std::array<uint8_t, 512>& dmx_data() const {
        return channel_data;
    }

    ~universe() {
        for (auto begin = srcs.begin(); begin != srcs.end(); ) {
            auto next { std::next(begin) };
            remove_source(begin->first);
            begin = next;
        }
    }
};

int systemd_event_timer_callback(sd_event_source* const s,
    std::uint64_t usec, void* userdata);
/**
 * Timer class - creates timers and runs callbacks
 */
class systemd_event_timer {
public:
    using key_type = sd_event*;
    using universe_type = universe<systemd_event_timer>;
private:
    universe_type& uni;
    std::unique_ptr<sd_event, deleters::sd_event> evloop;
    std::map<cid, std::unique_ptr<sd_event_source, deleters::sd_event_source>>
        timer_sources { };
    std::map<sd_event_source*, cid> sources_cid { };
public:
    systemd_event_timer(key_type key, universe_type* const u)
    : uni { *u }, evloop { key } {
        sd_event_ref(key);
    }
    systemd_event_timer(const systemd_event_timer& other) = delete;
    /* No implicit move constructor or move-assignment operator definition */
    systemd_event_timer& operator=(const systemd_event_timer& other) = delete;

    bool add(const cid& uuid, int millis) {
        assert(timer_sources.find(uuid) == timer_sources.end());

        int r;
        sd_event_source* src;

        if ((r = sd_event_add_time(evloop.get(), &src, CLOCK_MONOTONIC,
            static_cast<std::uint64_t>(millis)
            * UINT64_C(1000), 0, systemd_event_timer_callback,
            reinterpret_cast<void*>(this))) < 0) {
            return false;
        } else {
            timer_sources.try_emplace(uuid, src, deleters::sd_event_source { });
            sources_cid.try_emplace(src, uuid);
        }

        return true;
    }

    bool modify(const cid& uuid, int millis) {
        auto it { timer_sources.find(uuid) };
        assert(it != timer_sources.end());

        std::uint64_t now;

        if (sd_event_now(evloop.get(), CLOCK_MONOTONIC, &now) < 0)
            return false;

        if (sd_event_source_set_time(it->second.get(), now
            + static_cast<std::uint64_t>(millis)
            * UINT64_C(1000)) < 0)
            return false;

        return true;
    }

    bool remove(const cid& uuid) {
        auto it { timer_sources.find(uuid) };
        assert(it != timer_sources.end());

        sources_cid.erase(timer_sources[uuid].get());
        timer_sources.erase(uuid);

        return true;
    }

    void expired(sd_event_source* const s) {
        auto it { sources_cid.find(s) };
        assert(it != sources_cid.end());

        uni.timer_expired(it->second);
    }

    ~systemd_event_timer() {
    }
};

/**
 * Timer class callback
 */
int systemd_event_timer_callback(sd_event_source* const s,
    std::uint64_t usec, void* userdata) {
    auto& timer { *reinterpret_cast<systemd_event_timer*>(userdata) };

    timer.expired(s);

    return 0;
}
}

#endif /* E131_RECEIVER_HPP_ */
