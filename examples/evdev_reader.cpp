#include <libevdev/libevdev.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int, char** argv)
{
  struct libevdev *dev;
  int fd;
  int rc;
  
  fd = open(argv[1], O_RDONLY|O_NONBLOCK);

  if (fd < 0)
    fprintf(stderr, "error: %d %s\n", errno, strerror(errno));
  rc = libevdev_new_from_fd(fd, &dev);
  if (rc < 0)
    fprintf(stderr, "error: %d %s\n", -rc, strerror(-rc));

  printf("Device: %s\n", libevdev_get_name(dev));
  printf("vendor: %x product: %x\n",
         libevdev_get_id_vendor(dev),
         libevdev_get_id_product(dev));

  struct input_event ev;
  
  while (true)
  {
    rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
    
    if (rc < 0) {
      if (rc != -EAGAIN)
      {
        fprintf(stderr, "error: %d %s\n", -rc, strerror(-rc));
        break;
      }
    }
    else if (rc == LIBEVDEV_READ_STATUS_SUCCESS)
      printf("We have an event!\n%d (%s) %d (%s) value %d\n",
             ev.type, libevdev_event_type_get_name(ev.type),
             ev.code, libevdev_event_code_get_name(ev.type, ev.code),
             ev.value);
    else
        printf("return code: %d \n", rc);    
  }

  return 0;
}
