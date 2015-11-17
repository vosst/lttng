#ifndef EVDEV_H_
#define EVDEV_H_

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

namespace linux
{
namespace input
{
enum class Bus
{
  pci = BUS_PCI,
  isapnp = BUS_ISAPNP,
  usb = BUS_USB,
  hil = BUS_HIL,
  bluetooth = BUS_BLUETOOTH,
  _virtual = BUS_VIRTUAL,
  isa = BUS_ISA,
  i8042 = BUS_I8042,
  xtkbd = BUS_XTKBD,
  rs232 = BUS_RS232,
  gameport = BUS_GAMEPORT,
  parport = BUS_PARPORT,
  amiga = BUS_AMIGA,
  adb = BUS_ADB,
  i2c = BUS_I2C,
  host = BUS_HOST,
  gsc = BUS_GSC,
  atari = BUS_ATARI,
  spi = BUS_SPI
};

/// @brief operator<< pretty-prints the given bus enumerator to the given output stream.
std::ostream& operator<<(std::ostream& out, Bus bus);
}
}

namespace evdev
{
// @brief Error is the point-of-entry to the evdev Exception hierarchy.
class Error : public std::runtime_error
{
 public:
  /// @brief throw_if throws an Error instance if the given code indicates an error.
  static void throw_if(int code);

  /// @brief Error constructs a new instance for the given error code, providing
  /// a human-readable error description to the runtime_error.
  Error(int code);

 private:
  int code;
};

/// Poor man's quantity/unit system, mostly meant to provide type-safety. As soon 
/// as automatic conversation is needed, we should switch to something more powerful.
template<typename Unit, typename UnderlyingType = std::int32_t>
struct Quantity
{
  /// @brief The underlying numeric type.
  typedef UnderlyingType Type;

  /// @brief Constructs a new instance over the given raw value.
  explicit Quantity(Type value = Type()) : value{value}
  {
  }

  /// @brief casts this instance to the underlying numeric data type.
  operator Type() const
  {
    return value;
  }

  Type value; ///< The actual value of the quantity.
};

/// @brief operator< returns true iff the value of lhs is smaller than the value of rhs.
template<typename Unit, typename Type>
inline bool operator<(const Quantity<Unit, Type>& lhs, const Quantity<Unit, Type>& rhs)
{
  return lhs.value < rhs.value;
}

/// @brief Resolution bundles horizontal and vertical resolution
/// quantities, together with the templated unit that applies to both
/// values.
template<typename Unit>
struct Resolution
{
  Quantity<Unit> horizontal;
  Quantity<Unit> vertical;
};

/// @brief Size bundles a width and a height, together with the
/// templated unit that applies to both values.
template<typename Unit>
struct Size
{
  Quantity<Unit> width;
  Quantity<Unit> height;
};

namespace units
{
struct Pixel {};
struct PixelPerMillimeter{};
struct Millimeter{};
}

/// @brief resolution returns an instance of Resolution<Unit>. Just a
/// helper to avoid typing a lot of template boilerplate.
template<typename Unit>
Resolution<Unit> resolution(const Quantity<Unit>& horizontal, const Quantity<Unit>& vertical)
{
  return Resolution<Unit>{horizontal, vertical};
}

/// @brief resolution returns an instance of Size<Unit>. Just a
/// helper to avoid typing a lot of template boilerplate.
template<typename Unit>
Size<Unit> size(const Quantity<Unit>& width, const Quantity<Unit>& height)
{
  return Size<Unit>{width, height};
}

/// @brief pixels creates a Quantity<Pixel> from a raw integer value.
Quantity<units::Pixel> pixels(std::uint32_t value);

/// @brief millimeters creates a Quantity<Millimeter> from a raw integer value.
Quantity<units::Millimeter> millimeters(std::uint32_t value);

/// @brief pixels_per_millimeter creates a Quantity<units::PixelPerMillimeter> from a raw integer value.
Quantity<units::PixelPerMillimeter> pixels_per_millimeter(std::uint32_t value);

/// @brief DeviceDescription summarizes all common properties of an input device.
struct DeviceDescription
{
  std::string name; ///< The name of the input device.
  boost::optional<std::string> physical_location; ///< Its physical location.
  boost::optional<std::string> unique; ///< A unique identifier.
  std::int32_t product; ///< Numeric identifier of the product.
  std::int32_t vendor; ///< Numeric identifier of the vendor.
  std::int32_t version; ///< Version of the product.
  linux::input::Bus bus; ///< The bus that this input device is connected to.
};

/// @brief operator<< pretty-prints the given description to the given output stream.
std::ostream& operator<<(std::ostream&, const DeviceDescription&);

// A very simple helper struct that models a physical screen.
struct Screen
{
  Resolution<units::Pixel> resolution;
  Size<units::Millimeter> size;
};

 /// @brief Axis describes a single axis of a multi-touch device reporting absolute positions.
struct Axis
{
  struct Range
  {
    Range(const Quantity<units::Pixel>& minimum, const Quantity<units::Pixel>& maximum);
    
    Quantity<units::Millimeter> minimum;
    Quantity<units::Millimeter> maximum;
  };
  
  Axis(const Range& range);
  
  Range range; ///< Describes the available range of values.
  boost::optional<std::int32_t> fuzz;
  boost::optional<std::int32_t> flat;
  boost::optional<std::int32_t> resolution;
};

/// @brief Contact models an individual contact with a touch-capable device.
class Contact
{
 public:
  /// @brief Contact constructs a new contact with the given slot and id, belonging to the given uinput device.
  Contact(libevdev_uinput* uidev, std::uint32_t slot, std::uint32_t id);
  ~Contact();

  // @brief Moves the contact to the given coordinates.
  void move_to(std::uint32_t x, std::uint32_t y);

 private:
  libevdev_uinput* uidev;
  std::uint32_t slot;
  std::uint32_t id;
};

/// @brief Property enumerates all known device properties.
enum class Property
{
  needs_pointer = INPUT_PROP_POINTER, ///< Needs a pointer.
  is_direct = INPUT_PROP_DIRECT, ///< Is a direct input device.
  has_buttonpad = INPUT_PROP_BUTTONPAD, ///< Has button(s) under pad.
  is_semi_mt = INPUT_PROP_SEMI_MT ///< Provides touch rectangle only.
};

/// @brief Flags is a simple helper class that implements a subset of bitset operations
/// on top of an enumeration.
template<typename Enum>
class Flags
{
 public:
  /// @brief cast returns the numeric value of an individual enumerator.
  static typename std::underlying_type<Enum>::type cast(Enum value)
  {
    return static_cast<typename std::underlying_type<Enum>::type>(value);
  }

  /// @brief add alters the flags such that has(e) returns true.
  Flags<Enum>& add(Enum e)
  {
    value |= Flags<Property>::cast(e);
    return *this;
  }

  /// @brief add alters the flags such that has(e) returns true.
  Flags<Enum> add(Enum e) const
  {
    auto result = *this;
    result.add(e); return result;
  }

  /// @brief has returns true if the given enumerator is set in the flags.
  bool has(Enum e) const
  {
    return (value & Flags<Property>::cast(e)) != 0;
  }

 private:
  typename std::underlying_type<Enum>::type value;
};

typedef Flags<Property> Properties;

/// @brief Models an individual input event as reported by input devices.
struct Event
{
  std::chrono::microseconds when;
  std::uint16_t type;
  std::uint16_t code;
  std::int32_t value;
};

/// @brief operator<< pretty prints the given event to the given output stream.
std::ostream& operator<<(std::ostream& out, const Event& e);

/// @brief Device models an input device reporting events via the linux kernel input itf.
class Device
{
 public:

  /// @brief Device creates a new instance for the given fd.
  /// @throw evdev::Error in case of issues.
  Device(int fd);

  /// @brief Device creates a new instance for the given fd.
  /// @throw evdev::Error in case of issues.
  Device(const boost::filesystem::path& device_node);

  /// @brief fd returns the underlying file descriptor representing the device.
  int fd() const;

  /// @brief device_description queries the current state of the device configuration.
  DeviceDescription device_description() const;

  /// @brief read_next_event tries to read the next event reported by the input device.
  ///
  /// read_next_event never blocks, and returns an empty optional if there is no event available.
  /// @throws evdev::Error in case of issues.
  boost::optional<Event> read_next_event(bool sync = false);

 private:
  libevdev* dev;
};

/// @brief MultiTouchDevice models a "virtual" multi-touch device for testing purposes.
class MultiTouchDevice : public std::enable_shared_from_this<MultiTouchDevice>
{
 public:
  // All creation time properties go here.
  struct Configuration
  {
    DeviceDescription desc; ///< Describes common properties of a device.
    Size<units::Millimeter> size; ///< Describes the size of the device.
    Resolution<units::PixelPerMillimeter> resolution; ///< Describes the resolution of the device.
    Axis x; ///< Describes the x axis of the device.
    Axis y; ///< Describes the y axis of the device.
    Properties properties; ///< Describes further properties of the device.
  };

  // Creates an AbsoluteMultiTouchDevice "covering" the given screen, with the given resolution
  // of the touch screen.
  // Throw std::runtime_error if device creation fails.
  MultiTouchDevice(const Configuration& c);
  ~MultiTouchDevice() noexcept(true);

  /// @brief sys_path queries the sysfs device path.
  std::string sys_path() const;

  /// @brief device_ndoe queries the absolute path to the device node.
  std::string device_node() const;
  
  /// @brief add_contact adds another contact as recognized by this multi-touch device instance.
  std::shared_ptr<Contact> add_contact();

 private:
  // Iterates over the map of slots and finds the first empty one.
  std::pair<std::int32_t, std::int32_t> take_first_free_slot_and_generate_id();

  // Remember all the characteristics of the device.
  Configuration configuration;
  // libevdev-specific data structures go here.
  libevdev* dev;
  libevdev_uinput* uidev;
  // We have to keep track of available slots and identifiers.
  std::set<std::uint32_t> slots;
  std::uint32_t touch_identifier;
};
}

#endif // EVDEV_H_
