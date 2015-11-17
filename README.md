# lttng

This library provides C++(11) classes and functions for interacting with [lttng](https://www.lttng.org). On the one hand, the library enables developers to control and configure lttng trace generation from within C++(11) programs. On the other hand, the library provides support for reading and iterating traces in the [Common Trace Format](https://www.efficios.com/ctf), enabling easy processing of trace files.

The library is primarily used as a convenience tool to support integration- and acceptance-testing infrastructure, specifically for setups that span across multiple processes and additionally require capturing kernel behavior.

# Dependencies

- Boost
  - coroutine/context: For an easy async pattern.
  - filesystem: For handling anything filesystem.
  - system: Required by filesystem.
  - thread: Required by coroutine/context.
- babeltrace/babeltrace-ctf: For accessing CTF traces.
- [process-cpp](http://launchpad.net/process-cpp): For interaction with the lttng control application.

    On Ubuntu, you can install all required build- and run-time dependencies with:
    ```bash
    sudo apt-get install \
        libboost-dev libboost-filesystem-dev libboost-system-dev libboost-test-dev \
        libbabeltrace-dev libbabeltrace-ctf-dev \
        libprocess-cpp-dev
    ```
# Example
The following snippet illustrates analyzing multiple processes carrying out a task (memory allocation for demonstration purposes), capturing relevant trace points in a trace, and finally iterating the trace for calculating required statistics:
```cpp
namespace acc = boost::accumulators;

// Typedef our accumulator type.
typedef acc::accumulator_set<
  double,
  acc::stats<
    acc::tag::count, // The total count of events.
    acc::tag::min,   // Minimum value seen over all events.
    acc::tag::max,   // Maximum value seen over all events.
    acc::tag::mean,  // The mean value seen over all events.
    acc::tag::variance, // The variance in values seen over all events.
    acc::tag::density // Histogram over all values.
  >
> Statistics;

// Call like: LD_PRELOAD=liblttng-ust-libc-wrapper.so ./lttng-example
int main()
{
  // We instruct lttng to write traces to /tmp, more precisely: To a subdirectory
  // in /tmp of its own choice.
  auto consumer = std::make_shared<lttng::FileSystemConsumer>("/tmp/lttng-example");
  // We want to trace userspace events.
  auto ust_tracer = lttng::Tracer::create(lttng::Domain::userspace);
  // Create a tracing session.
  auto ust_session = ust_tracer->create_session("JustATestingSession", consumer);
  
  // Every event of the trace will carry this information.
  ust_session->add_context(lttng::Context::ip);
  ust_session->add_context(lttng::Context::proc_name);
  ust_session->add_context(lttng::Context::vpid);
  ust_session->add_context(lttng::Context::vtid);

  // Enable all libc and pthread events for recording purposes.
  ust_session->enable_event(lttng::events::userspace::libc::all);
  ust_session->enable_event(lttng::events::userspace::pthread::all);
  
  // Finally start the session.
  ust_session->start();

  std::vector<core::posix::ChildProcess> children;
  for (unsigned int i = 0; i < 5; i++)
    children.push_back(core::posix::fork([]() {
          std::default_random_engine rng;
          // We do allocations of size [1, 500].
          std::uniform_int_distribution<int> dist(1, 500);
                                           
          // Allocate memory a hundred times.
          for (unsigned i = 0; i < 100; i++)
            free(malloc(dist(rng)));
    
          return core::posix::exit::Status::success;
        }, core::posix::StandardStream::stderr));

  for(auto& child : children)
    child.wait_for(core::posix::wait::Flags::untraced);

  // Done, stopping the session.
  ust_session->stop();

  // We want to calcute the average size of malloc calls.
  Statistics malloc_size_stats(acc::tag::density::num_bins = 20, acc::tag::density::cache_size = 10);
  // Open the previously recorded trace.
  ctf::Trace trace(consumer->path());

  // Spec out the fields we are interested in:
  ctf::FieldSpec<ctf::Field::Type::integer> size{ctf::Scope::event_fields, "size"};
  ctf::FieldSpec<ctf::Field::Type::integer> vpid{ctf::Scope::stream_event_context, "vpid"};

  // Iterate over the trace.
  trace.for_each_event([&](const ctf::Event& event)
  {
    if (event.name == lttng::events::userspace::libc::malloc)
    {
      if (size.available_in(event))
        malloc_size_stats(size.interpret(event)->as_uint64());
    }

    return ctf::Trace::EventEnumeratorReply::ok;
  });

  // Statistics have been calculated, printing summary now:
  std::cout << acc::count(malloc_size_stats) << " " 
            << acc::mean(malloc_size_stats) << " " 
            << std::sqrt(acc::variance(malloc_size_stats)) << " "
            << acc::min(malloc_size_stats) << " "
            << acc::max(malloc_size_stats) << std::endl;

  // And print the histogram.
  auto hist = acc::density(malloc_size_stats);
  for (auto pair : hist)
    std::cout << pair.first << " " << pair.second << "\n";
  
  return 0;
```
