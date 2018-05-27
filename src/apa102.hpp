/**
 * \file apa102.hpp
 *
 * \copyright Shenghao Yang, 2018
 * 
 * See LICENSE for details
 */

#ifndef APA102_HPP_
#define APA102_HPP_

#include <gpiod.hpp>
#include <system_error>
#include <cstdint>
#include <vector>
#include <array>

namespace apa102 {
/**
 * APA102 start sequence, 4 bytes of zeroes.
 */
constexpr std::array<std::uint8_t, 4> start_sequence {
    0x00, 0x00, 0x00, 0x00
};

/**
 * Calculate the number of end bytes that must be clocked out in order to
 * terminate a LED update message.
 *
 * \param leds number of leds in the string LEDs.
 * \return number of end bytes required
 */
constexpr unsigned int end_bytes_required(unsigned int leds) {
    unsigned int edges_required = ((leds > 0) ? (leds - 1) : leds);
    return (edges_required % 16) ? ((edges_required / 16) + 1)
            : (edges_required / 16);
}

/**
 * Structure containing output information for a single LED.
 *
 * Modeled after the 4-byte LED command that needs to be sent to the
 * APA102 LEDs to change their output.
 */
struct output {
private:
    ::std::uint8_t hdr : 3;         ///< Header for LED output sequence
public:
    ::std::uint8_t brt : 5;         ///< LED brightness
    ::std::uint8_t blue;            ///< LED blue channel
    ::std::uint8_t green;           ///< LED green channel
    ::std::uint8_t red;             ///< LED red channel

    output(uint8_t brightness, uint8_t r, uint8_t g, uint8_t b):
        hdr {0b111}, brt {brightness}, blue {b}, green {g},
        red {r} {
    }

    bool operator==(const output& other) {
        return (brt == other.brt) && (blue == other.blue)
            && (green == other.green) && (red == other.red);
    }

    bool operator!=(const output& other) {
        return !(*this == other);
    }

} __attribute__((packed));

static_assert(sizeof(output) == 0x04, "Size of output structure "
        "is not 4 bytes - packing failure.");
/**
 * Class used to control APA102 LEDs connected to device I/O lines.
 *
 * Emulates the container functionality of \ref std::array, with each element
 * of the array containing one \ref output element.
 */
class apa102 {
private:
    using framebuffer_type = std::vector<output>;

    gpiod::line_bulk lines;
    framebuffer_type framebuffer;
    std::vector<int> line_states;

    unsigned int end_bytes;
    /**
     * Write a particular byte to the LEDs
     * \param b byte to write to the LEDs
     * \throws std::system_error on failure in process of setting GPIO lines.
     */
    void write_byte(const uint8_t b) {
        for (int i = 7; i > -1; i--) {
            uint8_t bit = ((b >> i) & 0x01);
            line_states[0] = 0;
            line_states[1] = bit;
            lines.set_values(line_states);
            line_states[0] = 1;
            lines.set_values(line_states);
        }
    }

    /**
     * Write the start sequence to the LEDs
     *
     * \throws std::system_error on failure in process of setting GPIO lines.
     */
    void write_start() {
        for (uint8_t b : start_sequence) {
            write_byte(b);
        }
    }

    /**
     * Write the LED output setting sequence to the LEDs
     *
     * \throws std::system_error on failure in process of setting GPIO lines.
     */
    void write_output() {
        auto start = reinterpret_cast<const std::uint8_t*>(
                framebuffer.data());
        auto end = reinterpret_cast<const std::uint8_t* const>(
                framebuffer.data() + framebuffer.size());
        while (start != end) {
            write_byte(*start);
            ++start;
        }
    }

    /**
     * Write the LED update end sequence to the LEDs
     *
     * \throws std::system_error on failure in process of setting GPIO lines.
     */
    void write_end() {
        for (unsigned int i = 0; i < end_bytes; i++) {
            write_byte(0x00);
        }
    }
public:
    /**
     * Construct a new object representing a string of APA102 LEDs.
     *
     * \param chip_path path to gpiochip device representing a gpio controller
     * or device whose gpio lines are attached to the clock and data lines of
     * the APA102 LEDs.
     * \param leds number of LEDs in the string.
     * \param clk LED clock line offset
     * \param data LED data line offset
     * \param period clock waveform period, in nanoseconds
     * \param reset whether to reset all LEDs to blank output
     * \throws ::std::system_error on failure in process of
     * acquiring gpio line control, or on failure to reset LEDs.
     */
    apa102(const std::string& chip_path, unsigned int leds,
           uint8_t clk, uint8_t data, bool reset = false):
        lines { },
        framebuffer { leds, output { 0, 0, 0, 0 } },
        line_states { 0x00, 0x00 }, end_bytes { end_bytes_required(leds) } {
        auto chip { gpiod::chip(chip_path, gpiod::chip::OPEN_BY_PATH) };
        ::std::vector<unsigned int> offsets { clk, data };
        lines = chip.get_lines(offsets);

        auto req = gpiod::line_request {
            "APA102",
            gpiod::line_request::DIRECTION_OUTPUT,
            std::bitset<32> { }
        };
        lines.request(req, ::std::vector<int> {0, 0});
        if (reset) {
            commit();
        }
    }
    /*
     * Delete copy constructor and copy assignment operator - each object
     * models a physical LED string, and we can't have two objects referring
     * to the same string - it just doesn't make sense.
     */
    apa102(const output& other) = delete;
    apa102& operator=(const output& other) = delete;
    /*
     * Move constructor and move assignment operator not implicitly
     * defined because copy constructor is user-defined.
     */

    /**
     * Access the output setting of a particular LED.
     *
     * \param led index of the LED to access. Must be within
     * the range \c 0 to the number of LEDs in the string - 1
     * \return output structure representing the output setting of that
     * particular LED.
     */
    output& operator[](::std::size_t led) {
        return framebuffer[led];
    }

    /**
     * Access the output setting of a particular LED.
     *
     * Same as the other \code operator[] overload for this object,
     * but returns a constant reference instead.
     */
    const output& operator[](::std::size_t led) const {
        return framebuffer[led];
    }


    /**
     * See \ref ::std::array::begin()
     */
    auto begin() {
        return framebuffer.begin();
    }

    /**
     * See \ref ::std::array::cbegin()
     */
    auto cbegin() const {
        return framebuffer.cbegin();
    }

    /**
     * See \ref ::std::array::end()
     */
    auto end() {
        return framebuffer.end();
    }

    /**
     * See \ref ::std::array::cend()
     */
    auto cend() const {
        return framebuffer.cend();
    }

    /**
     * See \ref ::std::array::rbegin()
     */
    auto rbegin() {
        return framebuffer.rbegin();
    }

    /**
     * See \ref ::std::array::crbegin()
     */
    auto crbegin() const {
        return framebuffer.crbegin();
    }

    /**
     * See \ref ::std::array::rend()
     */
    auto rend() {
        return framebuffer.rend();
    }

    /**
     * See \ref ::std::array::crend()
     */
    auto crend() const {
        return framebuffer.crend();
    }

    /**
     * See \ref ::std::array::size()
     */
    auto size() const {
        return framebuffer.size();
    }

    /**
     * See \ref ::std::array::fill()
     */
    void fill(const output& v) {
        for (auto& o: *this) {
            o = v;
        }
    }

    /**
     * Commit changes to the framebuffer to the actual LEDs.
     *
     * \throws ::std::system_error on error while writing to the LEDs
     */
    void commit() {
        write_start();
        write_output();
        write_end();
    }

    /**
     * Destructor for the apa102 object.
     *
     * Relases the I/O lines reserved for the APA102 LEDs.
     */
    ~apa102() {
        lines.release();
    }
};
}



#endif /* APA102_HPP_ */
