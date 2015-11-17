#include "evdev.h"

#include <core/posix/exit.h>
#include <core/posix/fork.h>

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

#include <csignal>
#include <fstream>
#include <thread>

namespace
{
evdev::Resolution<evdev::units::PixelPerMillimeter> the_optimal_touch_screen_resolution(const evdev::Screen& screen)
{
  return evdev::resolution(evdev::pixels_per_millimeter(screen.resolution.horizontal / screen.size.width),
                           evdev::pixels_per_millimeter(screen.resolution.vertical / screen.size.height));
}

struct TouchPoint
{
  std::int32_t x{-1}, y{-1}, slot{0}, id{0};
};

struct TouchPointAccumulator
{
  bool update(const evdev::Event& e)
  {
    if (e.type == EV_SYN && e.code == SYN_REPORT)
      return true;

    if (e.type == EV_ABS)
    {
      switch (e.code)
      {
        case ABS_MT_POSITION_X:
          current.x = e.value;
          break;
        case ABS_MT_POSITION_Y:
          current.y = e.value;
          break;
        case ABS_MT_SLOT:
          current.slot = e.value;
          break;
        case ABS_MT_TRACKING_ID:
          current.id = e.value;
          break;
      }
    }

    return false;
  }
  
  TouchPoint current;
};
}

int main(int, char**)
{
  // We are considering a phone screen here.
  evdev::Screen screen
  {
    evdev::resolution(evdev::pixels(1920), evdev::pixels(1080)),
    evdev::size(evdev::millimeters(600), evdev::millimeters(1100))
  };

  evdev::Resolution<evdev::units::PixelPerMillimeter> perfect_resolution = the_optimal_touch_screen_resolution(screen);
    
  evdev::DeviceDescription desc
  {
    "JustATestingDevice",
    std::string{"SomewhereInSpaceAndTime"},
    std::string{"UniquenessIsOptional"},
    42,
    42,
    42,
    linux::input::Bus::usb
  };

  auto touch_screen = std::make_shared<evdev::MultiTouchDevice>(evdev::MultiTouchDevice::Configuration
    {
      desc, 
      screen.size, 
      perfect_resolution, 
      evdev::Axis{evdev::Axis::Range{evdev::pixels(0), evdev::pixels(screen.resolution.horizontal)}},
      evdev::Axis{evdev::Axis::Range{evdev::pixels(0), evdev::pixels(screen.resolution.vertical)}},
      evdev::Properties().add(evdev::Property::is_direct)
    });

  // Print some information about the touchscreen
  std::cout << "sys path: " << touch_screen->sys_path() << std::endl;
  std::cout << "dev node: " << touch_screen->device_node() << std::endl;

  auto device_node = touch_screen->device_node();

  auto cp = core::posix::fork([device_node]() {      
      evdev::Device device(device_node);
      boost::asio::io_service io_service;
      boost::asio::io_service::work keep_alive{io_service};

      // Our signal-handling coroutine, monitoring for sigterm.
      boost::asio::spawn(io_service, [&io_service](boost::asio::yield_context yc) {
          boost::asio::signal_set ss(io_service, SIGTERM);

          if (SIGTERM == ss.async_wait(yc))
            io_service.stop();          
        });

      // Our reading coroutine, monitoring the underlying fd for
      // readability and processing events.
      boost::asio::spawn(io_service, [&io_service, &device](boost::asio::yield_context yc) {
          std::ofstream plot{"plot.txt"}; TouchPointAccumulator acc;

          boost::asio::posix::stream_descriptor sd(io_service, ::dup(device.fd()));

          // Just keep on running, async_read_some with the yield will make sure
          // that control is given to the next coroutine.
          for (;;)
          {
            boost::system::error_code ec;
            sd.async_read_some(boost::asio::null_buffers(), yc[ec]);

            // We return immediately if we encounter an error.
            if (ec)
              return;
            
            // We consume all events once we have identified the fd as readable.
            while (true)
            {
              try
              {        
                // read_next_event throws in case of severe issues with the
                // the underlying fd.
                if (auto event = device.read_next_event())
                {
                  std::cout << *event << std::endl;
                  if (acc.update(*event))
                    plot << acc.current.x << " " << acc.current.y << " " << std::sqrt(acc.current.id) << " " << acc.current.slot << std::endl;
                }
                else
                  break;
              }
              catch (const evdev::Error& e)
              {
                std::cerr << e.what() << std::endl;
                return;
              }
            }
          }
        });
      
      std::cout << "Device: " << device.device_description() << std::endl;

      io_service.run();

      return core::posix::exit::Status::success;
  }, core::posix::StandardStream::empty);

  std::string in; std::cin >> in;

  auto thumb = touch_screen->add_contact(); thumb->move_to(10, 10);

  for (unsigned int i = 11; i < 20; i++)
  {
    thumb->move_to(i, i);
  }

  auto index_finger = touch_screen->add_contact(); index_finger->move_to(20, 20);
  auto middle_finger = touch_screen->add_contact(); middle_finger->move_to(20, 20);

  std::cin >> in;

  cp.send_signal_or_throw(core::posix::Signal::sig_term);
  cp.wait_for(core::posix::wait::Flags::untraced);
  
  return 0;
}
