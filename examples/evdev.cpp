#include "evdev.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

std::ostream& linux::input::operator<<(std::ostream& out, linux::input::Bus bus)
{
  switch (bus)
  {
    case linux::input::Bus::pci: out << "pci"; break;
    case linux::input::Bus::isapnp: out << "isapnp"; break;
    case linux::input::Bus::usb: out << "usb"; break;
    case linux::input::Bus::hil: out << "hil"; break;
    case linux::input::Bus::bluetooth: out << "bluetooth"; break;
    case linux::input::Bus::_virtual: out << "_virtual"; break;
    case linux::input::Bus::isa: out << "isa"; break;
    case linux::input::Bus::i8042: out << "i8042"; break;
    case linux::input::Bus::xtkbd: out << "xtkbd"; break;
    case linux::input::Bus::rs232: out << "xtkbd"; break;
    case linux::input::Bus::gameport: out << "gameport"; break;
    case linux::input::Bus::parport: out << "parport"; break;
    case linux::input::Bus::amiga: out << "amiga"; break;
    case linux::input::Bus::adb: out << "adb"; break;
    case linux::input::Bus::i2c: out << "i2c"; break;
    case linux::input::Bus::host: out << "host"; break;
    case linux::input::Bus::gsc: out << "gsc"; break;
    case linux::input::Bus::atari: out << "atari"; break;
    case linux::input::Bus::spi: out << "spi"; break;
  }

  return out;
}

void evdev::Error::throw_if(int code)
{
  if (code == 0)
    return;

  throw Error(code);
}

evdev::Error::Error(int code) 
    : std::runtime_error(std::strerror(-code)), 
      code(code)
{
}

evdev::Quantity<evdev::units::Pixel> evdev::pixels(std::uint32_t value)
{
  return evdev::Quantity<evdev::units::Pixel>(value);
}

evdev::Quantity<evdev::units::Millimeter> evdev::millimeters(std::uint32_t value)
{
  return evdev::Quantity<evdev::units::Millimeter>(value);
}

evdev::Quantity<evdev::units::PixelPerMillimeter> evdev::pixels_per_millimeter(std::uint32_t value)
{
  return evdev::Quantity<evdev::units::PixelPerMillimeter>(value);
}

evdev::Axis::Range::Range(const evdev::Quantity<evdev::units::Pixel>& minimum, const evdev::Quantity<evdev::units::Pixel>& maximum)
    : minimum(minimum), maximum(maximum)
{
  if (minimum >= maximum)
    throw std::logic_error("Range: Constructing an empty range of values on an axis is not supported");
}

evdev::Axis::Axis(const evdev::Axis::Range& range)
    : range(range)
{
}

evdev::Contact::Contact(libevdev_uinput* uidev, std::uint32_t slot, std::uint32_t id)
    : uidev(uidev), slot(slot), id(id)
{
}

evdev::Contact::~Contact()
{
  static const std::uint32_t the_invalidating_tracking_id(-1);
  
  Error::throw_if(libevdev_uinput_write_event(uidev, EV_ABS, ABS_MT_SLOT, slot));
  Error::throw_if(libevdev_uinput_write_event(uidev, EV_ABS, ABS_MT_TRACKING_ID, the_invalidating_tracking_id));
  Error::throw_if(libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0));
  
  // TODO(tvoss): We have to hand the slot id back here.
}

void evdev::Contact::move_to(std::uint32_t x, std::uint32_t y)
{
  Error::throw_if(libevdev_uinput_write_event(uidev, EV_ABS, ABS_MT_SLOT, slot));
  Error::throw_if(libevdev_uinput_write_event(uidev, EV_ABS, ABS_MT_TRACKING_ID, id));
  Error::throw_if(libevdev_uinput_write_event(uidev, EV_ABS, ABS_X, x));
  Error::throw_if(libevdev_uinput_write_event(uidev, EV_ABS, ABS_MT_POSITION_X, x));
  Error::throw_if(libevdev_uinput_write_event(uidev, EV_ABS, ABS_Y, y));
  Error::throw_if(libevdev_uinput_write_event(uidev, EV_ABS, ABS_MT_POSITION_Y, y));
  Error::throw_if(libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0));
}

std::ostream& evdev::operator<<(std::ostream& out, const evdev::DeviceDescription& desc)
{
  out << "["
      << desc.name << " ";
  
  if (desc.physical_location)
    out << *desc.physical_location << " ";
  if (desc.unique)
    out << *desc.unique << " ";
  
  return out << "Product[" << desc.product << "]" << " "
             << "Vendor[" << desc.vendor << "]" << " "
             << "Version[" << desc.version << "]" << " "
             << "Bus[" << desc.bus << "]"
             << "]";
}

std::ostream& evdev::operator<<(std::ostream& out, const evdev::Event& e)
{
  return out << "[" 
             << e.when.count() << " "
             << libevdev_event_type_get_name(e.type) << " "
             << libevdev_event_code_get_name(e.type, e.code) << " "
             << e.value
             << "]";
}

evdev::Device::Device(int fd) : dev(nullptr)
{
  Error::throw_if(libevdev_new_from_fd(fd, &dev));
}

evdev::Device::Device(const boost::filesystem::path& device_node)
    : Device(::open(device_node.c_str(), O_RDONLY | O_NONBLOCK))
{
}

int evdev::Device::fd() const
{
  return libevdev_get_fd(dev);
}

evdev::DeviceDescription evdev::Device::device_description() const
{
  auto pl = libevdev_get_phys(dev);
  auto unique = libevdev_get_uniq(dev);

  return DeviceDescription{
      libevdev_get_name(dev),
      pl ? std::string(pl) : boost::optional<std::string>(),
      unique ? std::string(pl) : boost::optional<std::string>(),
      libevdev_get_id_product(dev),
      libevdev_get_id_vendor(dev),
      libevdev_get_id_version(dev),
      static_cast<linux::input::Bus>(libevdev_get_id_bustype(dev))};
}

boost::optional<evdev::Event> evdev::Device::read_next_event(bool sync)
{
  struct input_event ev;
  switch (libevdev_next_event(dev, sync ? LIBEVDEV_READ_FLAG_SYNC : LIBEVDEV_READ_FLAG_NORMAL, &ev))
  {
    case -EAGAIN:
      break;
    case LIBEVDEV_READ_STATUS_SYNC:
    case LIBEVDEV_READ_STATUS_SUCCESS:
      return Event{std::chrono::seconds(ev.time.tv_sec) + std::chrono::microseconds(ev.time.tv_usec), ev.type, ev.code, ev.value};
    default:
      break;
  }

  return boost::optional<Event>();
}

evdev::MultiTouchDevice::MultiTouchDevice(const Configuration& c)
    : configuration(c),
      dev(libevdev_new()),
      uidev(nullptr),
      slots({0, 1, 2, 3, 4, 5, 6, 7, 8, 9}),
      touch_identifier(0)
{
  libevdev_set_name(dev, configuration.desc.name.c_str());
  libevdev_set_id_product(dev, configuration.desc.product);
  libevdev_set_id_vendor(dev, configuration.desc.vendor);
  libevdev_set_id_version(dev, configuration.desc.version);
  if (configuration.desc.unique)
    libevdev_set_uniq(dev, configuration.desc.unique->c_str());
  if (configuration.desc.physical_location)
    libevdev_set_phys(dev, configuration.desc.physical_location->c_str());
  
  Error::throw_if(libevdev_enable_event_type(dev, EV_SYN));
  Error::throw_if(libevdev_enable_event_type(dev, EV_ABS));
  
  input_absinfo slot_info{ABS_MT_SLOT, 0, 10, 0, 0, 0};
  input_absinfo tracking_id_info{ABS_MT_TRACKING_ID, 0, std::numeric_limits<std::uint16_t>::max(), 0, 0, 0};
  
  Error::throw_if(libevdev_enable_event_code(dev, EV_ABS, ABS_MT_SLOT, &slot_info));
  Error::throw_if(libevdev_enable_event_code(dev, EV_ABS, ABS_MT_TRACKING_ID, &tracking_id_info));
  
  {
    input_absinfo info
    {
      ABS_MT_POSITION_X,
      static_cast<std::int32_t>(configuration.x.range.minimum), static_cast<std::int32_t>(configuration.x.range.maximum),
      configuration.x.fuzz ? static_cast<std::int32_t>(*configuration.x.fuzz) : std::int32_t{0},
      configuration.x.flat ? static_cast<std::int32_t>(*configuration.x.flat) : std::int32_t{0},
      configuration.x.resolution ? static_cast<std::int32_t>(*configuration.x.resolution) : std::int32_t{0}
    };

    Error::throw_if(libevdev_enable_event_code(dev, EV_ABS, ABS_MT_POSITION_X, &info));
  }
  {
    input_absinfo info
    {
      ABS_MT_POSITION_Y,
      configuration.y.range.minimum, configuration.y.range.maximum, 
      configuration.y.fuzz ? *configuration.y.fuzz : 0, 
      configuration.y.flat ? *configuration.y.flat : 0, 
      configuration.y.resolution ? *configuration.y.resolution : 0
    };
    
    Error::throw_if(libevdev_enable_event_code(dev, EV_ABS, ABS_MT_POSITION_Y, &info));
  }

  if (configuration.properties.has(Property::needs_pointer))
    Error::throw_if(libevdev_enable_property(dev, Properties::cast(Property::needs_pointer)));
  if (configuration.properties.has(Property::is_direct))
    Error::throw_if(libevdev_enable_property(dev, Properties::cast(Property::is_direct)));
  if (configuration.properties.has(Property::has_buttonpad))
    Error::throw_if(libevdev_enable_property(dev, Properties::cast(Property::has_buttonpad)));
  if (configuration.properties.has(Property::is_semi_mt))
    Error::throw_if(libevdev_enable_property(dev, Properties::cast(Property::is_semi_mt)));
  
  // Try to create the device and throw in case of issues.
  if (0 != libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev))
    throw std::runtime_error("TouchScreen::TouchScreen: Could not create uinput test device");
}
  
evdev::MultiTouchDevice::~MultiTouchDevice() noexcept(true)
{
  if (uidev)
    libevdev_uinput_destroy(uidev);

  if (dev)
    libevdev_free(dev);
}

std::string evdev::MultiTouchDevice::sys_path() const
{
  auto result = libevdev_uinput_get_syspath(uidev);
  return std::string(result ? result : "");
}

std::string evdev::MultiTouchDevice::device_node() const
{
  auto result = libevdev_uinput_get_devnode(uidev);
  return std::string(result ? result : "");
}
  
std::shared_ptr<evdev::Contact> evdev::MultiTouchDevice::add_contact()
{
  // We have to identify the first free slot here.
  auto pair = take_first_free_slot_and_generate_id();
  return std::make_shared<Contact>(uidev, pair.first, pair.second);
}

std::pair<std::int32_t, std::int32_t> evdev::MultiTouchDevice::take_first_free_slot_and_generate_id()
{
  auto it = slots.lower_bound(0);
    
  auto result = it != slots.end() ? *it : -1;
  slots.erase(it);

  return std::make_pair(result, ++touch_identifier);
}
