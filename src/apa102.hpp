/**
 * \file apa102.hpp
 *
 * Simple APA102 driver using Linux userspace SPI support.
 *
 * \copyright Shenghao Yang, 2018
 *
 * See LICENSE for details
 */

#ifndef APA102_HPP_
#define APA102_HPP_

#include <array>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <linux/types.h>
#include <sys/ioctl.h>
#include <system_error>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace apa102
{
/**
 * APA102 start sequence, 4 bytes of zeroes.
 */
constexpr std::array<std::uint8_t, 4> start_sequence{0x00, 0x00, 0x00, 0x00};

/**
 * Calculate the number of end bytes that must be clocked out in order to
 * terminate a LED update message.
 *
 * \param leds number of leds in the string LEDs.
 * \return number of end bytes required
 */
constexpr unsigned int
end_bytes_required(unsigned int leds)
{
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
  /**
   * Header for LED output sequence.
   *
   * \note *MUST* not be modified by the user.
   */
  /*
   * Additional note - I'd like for this header to be a private field,
   * but that does not guarantee a standard structure layout, i.e.
   * the header may not be the first field in the structure.
   * Having all members public ensures that the structure will be packed
   * in the obvious order. If that's the case the we can simply memcpy() to
   * our heart's content.
   */
  ::std::uint8_t hdr : 3;
  ::std::uint8_t brt : 5; ///< LED brightness
  ::std::uint8_t blue;    ///< LED blue channel
  ::std::uint8_t green;   ///< LED green channel
  ::std::uint8_t red;     ///< LED red channel
} __attribute__((packed));

/*
 * In the same vein, the operator overloads are declared externally to the
 * structure itself. This guarantees that the structure will have a standard
 * layout, and we can be sure of the order of the fields when we copy
 * the data into memory.
 */

/**
 * Check if two \ref output structures are the same.
 *
 * \param lhs first output structure.
 * \param rhs second output structure.
 * \retval true the output structures are the same.
 * \retval false the output structures are not the same.
 */
constexpr auto
operator==(const output& lhs, const output& rhs)
{
  return (lhs.brt == rhs.brt) && (lhs.blue == rhs.blue) &&
         (lhs.green == rhs.green) && (lhs.red == rhs.red);
}

/**
 * Check if two \ref output structures are different.
 *
 * \param lhs first output structure.
 * \param rhs second output structure.
 * \retval true output structures are different.
 * \retval false output structures are the same.
 */
constexpr auto
operator!=(const output& lhs, const output& rhs)
{
  return !(lhs == rhs);
}

static_assert(sizeof(output) == 0x04,
              "Size of output structure "
              "is not 4 bytes - packing failure.");

static_assert(std::is_standard_layout_v<output> == true,
              "Output structure does not have a standard layout - cannot"
              "perform raw memory operations on output structure");

/**
 * Creates an output structure from brightness and RGB components.
 *
 * \param brt LED global luminance setting, in range [0, 0x1f]
 * \param red LED luminance level for the red channel, in range [0, 0xff]
 * \param green LED luminance level for the green channel, in range [0, 0xff]
 * \param blue LED luminance level for the blue channel, in range [0, 0xff]
 * \return output structure filled with data provided as arguments.
 */
constexpr auto
make_output(std::uint8_t brt, std::uint8_t red, std::uint8_t green,
            std::uint8_t blue)
{
  return output{0b111, brt, blue, green, red};
}

/**
 * Class used to control APA102 LEDs connected to device I/O lines.
 */
class apa102
{
private:
  using framebuffer_type = std::vector<std::uint8_t>;

  int fd;
  std::size_t num_leds;
  framebuffer_type framebuffer;
  std::uint8_t* pixel_data_start{nullptr};
  spi_ioc_transfer xfer{};

public:
  /**
   * Construct a new object representing a string of APA102 LEDs.
   *
   * \param path path to userspace SPI device.
   * \param period clock waveform period, in nanoseconds
   * \param reset whether to reset all LEDs to blank output
   * \throws std::system_error on failure in process of acquiring control of
   * SPI device, or failure in resetting LEDs to blank.
   */
  apa102(const std::string& path, std::uint32_t period, std::size_t leds,
         bool reset = false)
      : fd{open(path.c_str(), O_RDWR)}, num_leds{leds}, framebuffer{}
  {
    framebuffer.resize(end_bytes_required(leds) + start_sequence.size() +
                           (sizeof(output) * leds),
                       0);
    pixel_data_start = {framebuffer.data() + start_sequence.size()};

    std::uint32_t spi_mode{SPI_MODE_0};
    std::uint8_t spi_lsbfirst{0};
    if ((fd == -1) || (ioctl(fd, SPI_IOC_WR_MODE32, &spi_mode) == -1) ||
        (ioctl(fd, SPI_IOC_WR_LSB_FIRST, &spi_lsbfirst) == -1))
      throw std::system_error{errno, std::system_category()};

    fill(make_output(0, 0, 0, 0));

    std::memset(reinterpret_cast<void*>(&xfer), 0, sizeof(xfer));
    xfer.tx_buf        = reinterpret_cast<__u64>(framebuffer.data());
    xfer.rx_buf        = reinterpret_cast<__u64>(nullptr);
    xfer.len           = framebuffer.size();
    xfer.speed_hz      = (UINT32_C(1000000000) / period);
    xfer.delay_usecs   = 0;
    xfer.bits_per_word = 8;
    xfer.cs_change     = 0;

    if (reset) commit();
  }

  apa102(const output& other)  = delete;
  apa102(const output&& other) = delete;
  apa102&
  operator=(const output& other) = delete;
  apa102&
  operator=(const output&& other) = delete;

  /**
   * Get the output setting of a particular LED.
   *
   * \param led index of the LED to access. Must be within
   * the range \c 0 to the number of LEDs in the string - 1
   * \return output structure representing the output setting of that
   * particular LED.
   */
  output operator[](std::size_t led) const
  {
    auto temp = make_output(0, 0, 0, 0);
    std::copy(pixel_data_start + (led * sizeof(temp)),
              pixel_data_start + ((led + 1) * sizeof(temp)),
              reinterpret_cast<std::uint8_t* const>(&temp));
    return temp;
  }

  /**
   * Set the output setting of a particular LED.
   *
   * \param led index of the LED to set the output setting for.
   * \param v desired output setting.
   */
  void
  set(std::size_t led, const output& v)
  {
    std::copy(reinterpret_cast<const std::uint8_t* const>(&v),
              reinterpret_cast<const std::uint8_t* const>(&v) + sizeof(v),
              pixel_data_start + (led * sizeof(v)));
  }

  /**
   * See \ref ::std::array::fill()
   */
  void
  fill(const output& v)
  {
    for (std::size_t i{0}; i < num_leds; i++) set(i, v);
  }

  /**
   * Commit changes to the framebuffer to the actual LEDs.
   *
   * \throws ::std::system_error on error while writing to the LEDs
   */
  void
  commit()
  {
    if (ioctl(fd, SPI_IOC_MESSAGE(1), &xfer) == -1)
      throw std::system_error{errno, std::system_category()};
  }

  /**
   * Obtain the number of LEDs controlled by this object.
   *
   * \return LED count.
   */
  std::size_t
  size() const
  {
    return num_leds;
  }

  /**
   * Destructor for the apa102 object.
   *
   * Closes the file descriptor used to access the userspace SPI device.
   */
  ~apa102()
  {
    if (fd != -1) close(fd);
  }
};
} // namespace apa102

#endif /* APA102_HPP_ */
