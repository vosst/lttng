#include <lttng/ctf.h>
#include <lttng/lttng.h>

#include <core/posix/fork.h>
#include <core/posix/wait.h>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/density.hpp>
#include <boost/accumulators/statistics/count.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/min.hpp>
#include <boost/accumulators/statistics/variance.hpp>

#include <iostream>
#include <thread>

namespace acc = boost::accumulators;

namespace
{
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
}

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

      //if (vpid.available_in(event))
      //std::cout << vpid.interpret(event)->as_int64() << std::endl;
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
}
