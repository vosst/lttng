#ifndef LTTNG_H_
#define LTTNG_H_

#include <boost/filesystem.hpp>
#include <boost/noncopyable.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

// Forward declare ctf elements.
namespace ctf
{
class Trace;
}

namespace lttng
{
namespace events
{
namespace userspace
{
// Enable all tracepoint providers and tracepoints in this namespace.
static constexpr const char* all{"ust_*"};
// liblttng-ust-libc-wrapper.so and liblttng-ust-pthread-wrapper.so
// can add instrumentation to respectively some C standard library and
// POSIX threads functions. Please see http://lttng.org/docs/#doc-liblttng‑ust‑libc‑pthread-wrapper
// for further details
namespace libc
{
  // Enable all tracepoints in this namespace.
  static constexpr const char* all{"ust_libc*"};

  static constexpr const char* mem_align{"ust_libc:posix_memalign"};
  static constexpr const char * memalign{"ust_libc:memalign"};
  static constexpr const char* realloc{"ust_libc:realloc"};
  static constexpr const char* calloc{"ust_libc:calloc"};
  static constexpr const char* free{"ust_libc:free"};
  static constexpr const char* malloc{"ust_libc:malloc"};
  static constexpr const char* soinfo{"ust_baddr_statedump:soinfo"};
}
namespace pthread
{
  // Enable all tracepoints in this namespace.
  static constexpr const char* all{"ust_pthread*"};

  static constexpr const char* mutex_lock_req{"ust_pthread:pthread_mutex_lock_req"};
  static constexpr const char* mutex_lock_acq{"ust_pthread:pthread_mutex_lock_acq"};
  static constexpr const char* mutex_trylock{"ust_pthread:pthread_mutex_trylock"};
  static constexpr const char* mutex_unlock{"ust_pthread:pthread_mutex_unlock"};
}
}
}

/// @brief Exception is the type thrown from lttng functions and methods in case of issues.
/// 
/// what() will return a human-readable error description of the issue.
struct Exception : public std::runtime_error
{
 public:
  Exception(int code) noexcept(true);

  int code; ///< We store the original error code reported by the lttng api.
};

/// @brief Domain enumerates the different tracing domains supported by lttng.
enum class Domain
{
  kernel,
  userspace
};

/// @brief operator<< pretty prints the given domain to the given output stream.
std::ostream& operator<<(std::ostream& out, Domain domain);

/// @brief A context is basically extra information appended to a
/// channel. For instance, you could ask the tracer to add the PID
/// information for all events in a channel. You can also add
/// performance monitoring unit counters (perf PMU) using the perf
/// kernel API.
enum Context
{
  pid,
  proc_name,
  prio,
  nice,
  vpid,
  tid,
  vtid,
  ppid,
  vppid,
  pthread_id,
  hostname,
  ip
};

/// @brief operator<< pretty prints the given context to the given output stream.
std::ostream& operator<<(std::ostream& out, Context context);

/// @brief Consumer models an arbitrary trace consumer.
class Consumer : public boost::noncopyable
{
 public:
  virtual ~Consumer() = default;

  /// @brief to_url returns a url that can be passed to an lttng session.
  virtual std::string to_url() const = 0;

 protected:
  // Only subclasses can instantiate.
  Consumer() = default;
};

/// @brief FileSystemConsumer implements Consumer, recording traces to the 
/// given directory in the file system.
class FileSystemConsumer : public Consumer
{
 public:
  /// @brief FileSystemConsumer constructs a new instance, creating the path if necessary.
  /// @throws if the given path is not writable.
  FileSystemConsumer(const boost::filesystem::path& path);
  
  /// @brief path returns the contained path.
  const boost::filesystem::path& path() const;

  /// @brief to_url returns the path in url format,i.e., with file:// prefixed.
  std::string to_url() const override;

 private:
  boost::filesystem::path path_;
};

/// @brief Session models an individual tracing session.
class Session : public boost::noncopyable
{
 public:
  /// @brief Creates a new session with the given name.
  Session(Domain domain, const std::string& name, const std::shared_ptr<Consumer>& consumer);

  /// @brief ~Session stops the tracing session.
  virtual ~Session();

  /// @brief name returns the name of the session.
  virtual const std::string& name() const;

  /// @brief add_context enables the given context for all enabled events in this session.
  virtual void add_context(Context ctxt);

  /// @brief enable_event enables the event with the given name in the given domain.
  /// @throws std::runtime_error in case of issues.
  virtual void enable_event(const std::string& event);

  /// @brief Starts the tracing.
  virtual void start();

  /// @brief Stops the tracing.
  virtual void stop();

 private:
  Domain domain_; ///< The domain of the tracing session.
  std::string name_; ///< The name of the tracing session.
  std::shared_ptr<Consumer> consumer_; ///< The trace consumer instance.
};

/// Tracer is the primary point of entry to the lttng-tracing functionality.
class Tracer
{
 public:
  /// @brief create returns a new unique tracer instance.
  static std::unique_ptr<Tracer> create(Domain domain);

  /// @brief Tracer creates a new tracer instance.
  Tracer(Domain domain);

  /// @brief ~Tracer cleans up and disables all lttng tracing functionality.
  virtual ~Tracer() = default;

  /// @brief create_session creates a new tracing session with the given name and the given consumer.
  virtual std::shared_ptr<Session> create_session(const std::string& name, const std::shared_ptr<Consumer>& consumer);

 private:
  Domain domain;
};
}

#endif // LTTNG_H_
