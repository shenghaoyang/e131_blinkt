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
#include <systemd/sd-journal.h>
#include <endian.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <functional>
#include <stdexcept>
#include <iostream>
#include <iterator>
#include <string>
#include <cstdint>
#include <memory>
#include <queue>
#include <array>
#include <map>

/**
 * Functionality crucial to the E1.31 receiver implementation in e131_blinkt.
 */
namespace e131_receiver {
/**
 * Type used to represent the 128 bit UUID of the source. Big Endian.
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
 * Obtain the string representation of a source UUID
 *
 * \param uuid UUID
 * \return string representation of the source UUID, in the form \code 0x<val>,
 * where \code <val> contains the zero-padded hexadecimal representation of
 * the UUID, starting with the most-significant byte.
 */
std::string cid_str(const cid& uuid);

/**
 * Simple class akin to \ref std::unique_ptr, but for file descriptors, and with
 * reduced functionality.
 */
class unique_fd {
private:
    int fd { -1 };
public:
    /**
     * Default constructor. Initializes internal fd storage to \code -1.
     */
    explicit unique_fd();
    /**
     * Constructs object from an existing file descriptor.
     *
     * Ownership of the file descriptor is transferred to this object.
     *
     * \param f file descriptor to use.
     */
    explicit unique_fd(int f);
    unique_fd(const unique_fd& other) = delete;
    unique_fd(const unique_fd&& other) = delete;
    unique_fd& operator=(const unique_fd& other) = delete;
    unique_fd& operator=(const unique_fd&& other) = delete;

    /**
     * Conversion to integer.
     *
     * Can be used to obtain the file descriptor stored in the object.
     */
    operator int();

    /**
     * Closes the file descriptor.
     */
    ~unique_fd();
};

/**
 * Used to track the priority of the highest priority source in an E1.31
 * universe.
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
     * Minimum E1.31 priority
     */
    static constexpr uint8_t minimum_priority { 0 };

private:
    std::map<priority_type, count_type> prio_cnt { };

public:
    /**
     * Sets priority to the default E1.31 priority and source count to zero.
     */
    priority();
    /* Allow default move constructor */
    /* Allow default copy constructor */
    /* Allow default copy-assign */
    /* Allow default move-assign */

    /**
     * Type-conversion to type representing current E1.31 priority.
     *
     * Allows users to obtain tracked priority level by using this object
     * in a context where a numeric type is required.
     */
    operator priority_type() const;

    /**
     * Add a new source priority.
     *
     * \param p priority of the source.
     * \return new priority level
     */
    priority_type add(priority_type p);

    /**
     * Remove a source priority.
     *
     * \param p priority of the source.
     * \return new priority level.
     */
    priority_type remove(priority_type p);

    /**
     * Obtain the number of sources with priority equivalent to the
     * current priority.
     *
     * \return source count.
     */
    count_type sources() const;
};

/**
 * Structure representing information regarding a particular source of
 * E1.31 DMX data.
 */
struct source {
    const cid uuid;                        ///< Source CID
    priority::priority_type prio;          ///< Priority at which the source broadcasts
    std::uint8_t sequence_data;            ///< Sequence of the last E1.31 data packet
    std::uint8_t sequence_synchronization; ///< Sequence of the last E1.31 sync packet
    std::unique_ptr<sd_event_source, deleters::sd_event_source> timer_evs;  ///< Data loss timer event source
};

/**
 * Event structure returned in vector provided by \ref universe::update()
 */
struct update_event {
    /**
     * Event type
     */
    enum event_type {
        CHANNEL_DATA_UPDATED, ///< DMX channel data updated
        SOURCE_ADDED,         ///< New source added
        SOURCE_REMOVED,       ///< Source removed
        SOURCE_LIMIT_REACHED, ///< Source limit reached, source not added
    } const event;
    /**
     * UUID of source involved in the event.
     */
    const cid id;

    update_event(event_type t, const cid& uuid)
    : event { t }, id { uuid } {
    }
};

struct channel_data_updated_event : public update_event {
    channel_data_updated_event(const cid& uuid);
};

struct source_added_event : public update_event {
    source_added_event(const cid& uuid);
};

struct source_removed_event : public update_event {
    source_removed_event(const cid& uuid);
};

struct source_limit_reached_event : public update_event {
    source_limit_reached_event(const cid& uuid);
};

/**
 * Object representing a particular E1.31 universe.
 *
 * Used to track sources and their priorities to decide from which source to
 * update DMX channel data from.
 */
class universe {
public:
    using channel_data_type = std::array<std::uint8_t, 512>;
private:
    priority prio { };                                        ///< Universe priority
    std::map<const cid, source> srcs { };                     ///< CID to source mapping
    std::map<sd_event_source* const,
        std::reference_wrapper<const cid>> evs_cid { };       ///< Event source to cid map
    channel_data_type channel_data { };                       ///< DMX channel data
    std::queue<cid> pending_removal { };                      ///< Sources pending removal
    std::vector<update_event> queued_events { };              ///< Events pending return
    priority::count_type max_sources;                         ///< Maximum source count
    bool ignore_preview_flag;                                 ///< Preview flag ignore
    int uni;                                                  ///< Watched universe number
    unique_fd e131_socket;                                    ///< E1.31 socket fd
    std::unique_ptr<sd_event, deleters::sd_event> ev;         ///< Systemd event loop

    /**
     * Add and track a particular source sending E1.31 data for the watched
     * universe.
     *
     * \param uuid UUID of the source to be added.
     * \param pkt initial E1.31 data packet from the source.
     * \throw source_limit_reached_event on reaching maximum source count.
     * \throw std::system_error on system-related errors on adding source.
     */
    void add_source(const cid& uuid, const e131_packet_t& pkt);

    /**
     * Reset the network data loss timer for a particular source.
     *
     * \param src source object.
     * \throw std::system_error on system-related errors on resetting timer.
     */
    void source_timer_reset(const source& src);

    /**
     * Untrack a particular source.
     *
     * The removal event will be pushed into \ref queued_events.
     *
     * \param src source object.
     */
    void remove_source(const source& src);

    /**
     * Checks if an E1.31 packet is valid, and should be processed further.
     *
     * An E1.31 packet is only considered valid if:
     * - The packet's data fields are within the limits set in the E1.31
     *   specification.
     * - The root layer protocol header in the packet contains a vector
     *   identifying it as an E1.31 DATA packet.
     * - The packet's E1.31 header identifies the packet as containing
     *   E1.31 data pertaining to the universe this object is tracking,
     * - The packet's preview flag is not set OR the ignore preview flag
     *   setting is set.
     *
     * \param pkt packet to inspect.
     * \retval true packet should be processed further.
     * \retval false packet processing should terminate.
     */
    bool valid_packet(e131_packet_t& pkt);

    /**
     * Callback to be called by the event loop on timer expiring.
     *
     * \see sd_event_add_time for more information regarding
     *      function arguments.
     * \retval 0 timer callback execution success.
     * \retval nonzero timer callback execution failure.
     */
    static int timer_callback(sd_event_source* const s, std::uint64_t usec,
        void* userdata);

    /**
     * Handle updates from the E1.31 socket.
     *
     * \param revents events bitmask from the I/O event callback.
     * \retval true successfully processed updates
     * \retval false unsuccessfully processed updates.
     */
    bool socket_handler(std::uint32_t revents);

    /**
     * Callback to be called by the event loop when data has been received
     * on the E1.31 socket.
     *
     * \see sd_event_add_io for more information regarding
     *      function arguments.
     * \retval 0 callback execution success.
     * \retval nonzero callback execution failure.
     */
    static int socket_callback(sd_event_source* s, int fd,
        std::uint32_t revents, void* userdata);
public:
    /**
     * Initialize a universe object.
     *
     * \param sources maximum number of sources to register.
     * \param preview_flag_ignore whether to ignore the preview flag in
     *        E1.31 data packets.
     * \param universe_num the universe number assigned to the universe this
     *        object is tracking.
     * \throws std::system_error on system failures.
     */
    universe(priority::count_type sources, bool preview_flag_ignore,
        int universe_num);
    universe(const universe& other) = delete;
    universe(const universe&& other) = delete;
    universe& operator=(const universe& other) = delete;
    universe& operator=(const universe&& other) = delete;

    /**
     * Obtain a file descriptor that can be polled for \code POLLIN or
     * \code EPOLLIN events, to signal when to call the \ref update()
     * member function.
     *
     * \return non-negative file descriptor on success, negative errno-style
     *         error code on failure.
     */
    int event_fd() const;

    /**
     * Process data for the universe tracker.
     *
     * To be called when the file descriptor associated with \ref event_fd()
     * can be read from.
     *
     * \param pkt E1.31 packet
     * \return vector of \ref update_event objects.
     * \throws std::runtime_error on failures when adding / removing / modifying
     *         sources, due to errors not related to source count limiting.
     * \throws std::system_error on system related failures.
     * \note When any exception has occurred, the E1.31 receiver is considered
     *       to be in a degraded state. No non-const operations may then be
     *       performed on the receiver object.
     */
    const std::vector<update_event>& update();

    /**
     * Obtain the maximum source priority among all sources registered.
     *
     * \return maximum source priority.
     */
    priority::priority_type max_priority() const;

    /**
     * Obtain the number of sources sending data at the maximum priority.
     *
     * \return number of sources sending data at the maximum priority.
     */
    priority::count_type max_priority_sources() const;

    /**
     * Obtain the DMX channel data, updated from the most recent
     * call to \ref update() with a \ref channel_data_update_event returned.
     *
     * \return DMX channel data represented as an array of 512 bytes.
     */
    const channel_data_type& dmx_data() const;
};
}

#endif /* E131_RECEIVER_HPP_ */
