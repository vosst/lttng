#ifndef CTF_H_
#define CTF_H_

#include <babeltrace/babeltrace.h>
#include <babeltrace/ctf/callbacks.h>
#include <babeltrace/ctf/events.h>
#include <babeltrace/ctf/iterator.h>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <boost/variant.hpp>

#include <chrono>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace ctf
{
/// @brief Void explicitly models the empty type.
struct Void {};

/// @brief Top-level scopes in CTF.
enum class Scope
{
  trace_packet_header,
  stream_packet_context,
  stream_event_header,
  stream_event_context,
  event_context,
  event_fields
};

/// @brief Returns an iterateable vector of scopes.
const std::vector<Scope>& scopes();

/// @brief operator< returns true iff the numeric value of lhs is less than the numeric value of rhs.
bool operator<(Scope lhs, Scope rhs) noexcept(true);

/// @brief operator<< pretty prints the given scope to the given output stream.
inline std::ostream& operator<<(std::ostream& out, Scope scope);

class Integer
{
 public:
  /// @brief Value models either a signed or unsigned integer type.
  typedef boost::variant<Void, std::int64_t, std::uint64_t> Value;

  /// @brief Constructs the default, empty instance. Attempting to access the values will throw.
  Integer();
  /// @brief Explicit construction from a given 64-bit signed integer value.
  explicit Integer(std::int64_t i, std::uint8_t width, std::uint64_t base);
  /// @brief Explicit construction from a given 64-bit unsigned integer value.
  explicit Integer(std::uint64_t u, std::uint8_t width, std::uint64_t base);

  /// @brief The width of the integer in bits.
  std::uint8_t width() const;

  /// @brief The base of the integer.
  std::uint64_t base() const;

  /// @brief Returns true if the contained value is signed.
  bool is_signed() const;

  /// @brief Returns true if the contained value is void.
  bool is_empty() const;

  /// @brief as_int64 tries to extract an int64 from the given Integer.
  /// @throws in case of issues.
  std::int64_t as_int64() const;

  /// @brief as_uint64 tries to extract an unsigned int64 from the given Integer.
  /// @throws in case of issues.
  std::uint64_t as_uint64() const;

 private:
  std::uint8_t width_; ///< The width of the integer, in bits.
  std::uint64_t base_; ///< The numeric base of the integer.
  Value value_; ///< The actual value.
};

/// @brief Enumerator models a single value of an enumeration.
struct Enumerator
{
  std::string as_string; ///< The textual representation of the enumerator.
  Integer as_integer; ///< The integer representation of the enumerator.
};

/// @brief Field models an individual field in an event.
struct Field
{
  /// @brief Type enumerates all known types that can be contained in a field.
  enum Type
  {
    unknown = 0,
    integer,
    floating_point,
    enumeration,
    string,
    structure,
    untagged_variant,
    variant,
    array,
    sequence
  };

  /// @brief Variant carries the values of a field.
  typedef boost::make_recursive_variant
  <
    Void,  // boost::variant guarantees never empty. However, we would like to have a designated 'not set' value.
    Integer, // An integer is contained within this field's value.
    double, // A floating-point value is contained within this field's value.
    Enumerator, // An enumerator is contained within this field's value.
    std::string, // A string is contained within this field's value.
    boost::recursive_variant_, // A variant can contain itself, being a so-called boxed-type.
    std::vector<boost::recursive_variant_> // A variant can be a collection of arbitrary values.
  >::type Variant;

  /// @brief Field constructs a new instance with the given parameters.
  /// @throws in case of issues.
  Field(const std::string& name, Type type, const Variant& value);

  /// @brief type returns the type of the value contained in this field.
  Type type() const;

  // @brief is_a returns true if the contained value is of the given type.
  bool is_a(Type type) const;

  /// @brief name returns a const reference to the name of the field.
  const std::string& name() const;

  /// @brief value returns a const reference to the value of the field.
  const Variant& value() const;

  /// @brief as_integer tries to interpret the contained value as an integer.
  /// @brief throws boost::bad_cast in case of issues.
  const Integer& as_integer() const;

  /// @brief as_floating_point tries to interpret the contained value as a double.
  /// @brief throws boost::bad_cast in case of issues.
  double as_floating_point() const;

  /// @brief as_enumerator tries to interpret the contained value as an Enumerator instance.
  /// @brief throws boost::bad_cast in case of issues.
  const Enumerator& as_enumerator() const;

  /// @brief as_string tries to interpret the contained value as a string.
  /// @brief throws boost::bad_cast in case of issues.
  const std::string& as_string() const;

  /// @brief unwrap tries to interpret the contained value as a variant.
  /// @brief throws boost::bad_cast in case of issues.
  const Variant& unwrap() const;

  /// @brief unwrap tries to interpret the contained value as a collection of Variant instances.
  /// @brief throws boost::bad_cast in case of issues.
  const std::vector<Variant>& as_collection() const;

 private:
  std::string name_;
  Type type_;
  Variant value_;
};

template<Field::Type type>
struct TypeMapper
{
  typedef Void Type;
  
  static const Type& extract(const Field& f);
};

template<>
struct TypeMapper<Field::Type::integer>
{
  typedef Integer Type;

  static const Integer& extract(const Field& f)
  {
    return f.as_integer();
  }
};

template<>
struct TypeMapper<Field::Type::floating_point>
{
  typedef double Type;

  static double extract(const Field& f)
  {
    return f.as_floating_point();
  }
};

template<>
struct TypeMapper<Field::Type::enumeration>
{
  typedef Enumerator Type;

  static const Enumerator& extract(const Field& f)
  {
    return f.as_enumerator();
  }
};

template<>
struct TypeMapper<Field::Type::string>
{
  typedef std::string Type;

  static const std::string& extract(const Field& f)
  {
    return f.as_string();
  }
};

template<>
struct TypeMapper<Field::Type::structure>
{
  typedef std::vector<Field::Variant> Type;

  static const std::vector<Field::Variant>& extract(const Field& f)
  {
    return f.as_collection();
  }
};

template<>
struct TypeMapper<Field::Type::untagged_variant>
{
  typedef Field::Variant Type;

  static const Field::Variant& extract(const Field& f)
  {
    return f.unwrap();
  }
};

template<>
struct TypeMapper<Field::Type::variant>
{
  typedef Field::Variant Type;

  static const Field::Variant& extract(const Field& f)
  {
    return f.unwrap();
  }
};

template<>
struct TypeMapper<Field::Type::array>
{
  typedef std::vector<Field::Variant> Type;

  static const std::vector<Field::Variant>& extract(const Field& f)
  {
    return f.as_collection();
  }
};

template<>
struct TypeMapper<Field::Type::sequence>
{
  typedef std::vector<Field::Variant> Type;

  static const std::vector<Field::Variant>& extract(const Field& f)
  {
    return f.as_collection();
  }
};

/// @brief Pretty prints the given integer to the given output stream.
std::ostream& operator<<(std::ostream& out, const Integer& integer);

/// @brief Pretty prints the given enumerator to the given output stream.
std::ostream& operator<<(std::ostream& out, const Enumerator& enumerator);

/// @brief Pretty prints the given field type to the given output stream.
std::ostream& operator<<(std::ostream& out, const Field::Type& type);

/// @brief Pretty prints the given Void instance to the given output stream.
inline std::ostream& operator<<(std::ostream& out, const Void&);

/// @brief Pretty prints the given vector to the given output stream.
template<typename T>
inline std::ostream& operator<<(std::ostream& out, const std::vector<T>& vector)
{
  for (const auto& value : vector)
    out << value << " ";
  
  return out;
}

/// @brief Pretty prints the given Variant instance to the given output stream.
std::ostream& operator<<(std::ostream& out, const Field::Variant& variant);

/// @brief Pretty prints the given field to the given output stream.
std::ostream& operator<<(std::ostream& out, const Field& field);

/// @brief Event models an individual event recorded in a trace.
struct Event
{
  /// @brief An event contains a map of key-value pairs. This is the key type.
  typedef std::tuple<Scope, std::string> Key;
  /// @brief An event contains a map of key-value pairs. This is the value type.
  typedef Field Value;
  /// @brief An event contains fields, which are key-value pairs.
  typedef std::map<Key, Value> Fields;

  std::string name; ///< The name of the event. May be empty.
  std::uint64_t cycles; ///< The timestamp of the event as written in the packet (in cycles).
  std::chrono::nanoseconds timestamp; ///< The timestamp of the event, in nanoseconds since the epoch.
  Fields fields; ///< The payload of the event.
};

/// @brief operator<< pretty prints the given Event instance to the given output stream.
std::ostream& operator<<(std::ostream& out, const Event& event);

/// @brief FieldSpec helps in describing an individual field of an event,
/// such that interpretation and query for values becomes more convenient.
template<Field::Type type>
class FieldSpec
{
 public:
  /// @cond
  typedef typename TypeMapper<type>::Type Type;
  typedef boost::optional<Type> OptionalType;
  /// @endcond

  /// @brief Creates a new instance with the given scope and name.
  FieldSpec(Scope scope, const std::string& name)
      : scope(scope), 
        name(name)
  {
  }

  /// @brief available_in returns true if the given event contains the field
  /// described by this spec.
  bool available_in(const Event& e) const
  {
    auto it = e.fields.find(std::make_tuple(scope, name));

    if (it == e.fields.end())
      return false;

    return it->second.is_a(type);
  }

  /// @brief interpret tries to interpret the value from the given event.
  ///
  /// @returns an empty optional if available_in(...) is false, the contained value otherwise.
  OptionalType interpret(const Event& e) const
  {
    if (not available_in(e))
      return OptionalType{};

    return OptionalType{interpret_or_throw(e)};
  }

  /// @brief interpret_or_throw tries to extract the field from the given event.
  /// @throws in case of issues.
  /// @returns the value contained within the given event.
  const typename TypeMapper<type>::Type& interpret_or_throw(const Event& e) const
  {
    return TypeMapper<type>::extract(e.fields.at(std::make_tuple(scope, name)));
  }

 private:
  Scope scope;
  std::string name;
};

/// @brief Trace models an individul recording of events in CTF (Common Trace Format).
class Trace
{
 public:
  /// @brief EventEnumeratorReply lists the possible return values of an EventEnumerator.
  enum EventEnumeratorReply
  {
    ok, ///< Processing of the event went fine, keep on going.
    stop, ///< Processing of the event went fine, but stop enumeration.
    stop_with_error, ///< Something went wrong while processing the event, stop.
    continue_with_error ///< Something went wrong, keep on going though.
  };

  /// @brief EventEnumerator is a functor that is passed to for_each_event and invoked
  /// for every event in a trace.
  typedef std::function<EventEnumeratorReply(const Event&)> EventEnumerator;
  
  /// @brief Trace creates a new instance, loading data from the given path.
  /// @throws if opening the trace fails.
  Trace(const boost::filesystem::path& path);
  Trace(const Trace&) = delete;
  Trace(Trace&&) = delete;
  virtual ~Trace() = default;
  
  Trace& operator=(const Trace&) = delete;
  Trace& operator=(Trace&&) = delete;

  /// @brief for_each_event iterates over this trace, invoking the given enumerator for every event.
  virtual void for_each_event(EventEnumerator enumerator);

 private:
  boost::filesystem::path path_;
  bt_context* context;
  int trace_handle;
};
}

#endif // CTF_H_
