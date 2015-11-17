#include <lttng/ctf.h>

#include <iomanip>

namespace
{
bt_cb_ret to_c_api(ctf::Trace::EventEnumeratorReply reply)
{
  switch (reply)
  {
    case ctf::Trace::EventEnumeratorReply::ok:
      return BT_CB_OK;
    case ctf::Trace::EventEnumeratorReply::stop:
      return BT_CB_OK_STOP;
    case ctf::Trace::EventEnumeratorReply::stop_with_error:
      return BT_CB_ERROR_STOP;
    case ctf::Trace::EventEnumeratorReply::continue_with_error:
      return BT_CB_ERROR_CONTINUE;
  }

  throw std::logic_error("to_c_api: we should never reach here.");
}

// CallbackContext encapsulates handling of event callbacks issued by babeltrace for individual events in a trace.
struct CallbackContext
{
  // Tries to extract a ctf::Integer instance from the given def/decl pair.
  // Throws std::runtime_error in case of issues.
  static ctf::Integer process_integer_field_value(const bt_ctf_event*, const bt_definition* def, const bt_declaration* decl)
  {
    auto base = bt_ctf_get_int_base(decl);
    auto width = bt_ctf_get_int_len(decl);

    switch (bt_ctf_get_int_signedness(decl))
    {
      case 0: // unsigned
        return ctf::Integer(bt_ctf_get_uint64(def), width, base);
      case 1: // signed
        return ctf::Integer(bt_ctf_get_int64(def), width, base);
      default:
        break;
    }

    return ctf::Integer();
  }

  // Tries to extract a double from the given def/decl pair.
  // Throws std::runtime_error in case of issues.
  static double process_float_field_value(const bt_ctf_event*, const bt_definition* def, const bt_declaration*)
  {
    auto result = bt_ctf_get_float(def);

    if (bt_ctf_field_get_error() < 0)
      throw std::runtime_error("Error while interpreting floating point value");

    return result;
  }

  // Tries to extract an enumerator from the given def/decl pair.
  // Throws std::runtime_error if there was an error extracting the required data.
  static ctf::Enumerator process_enum_field_value(const bt_ctf_event* event, const bt_definition* def, const bt_declaration*)
  {
    ctf::Enumerator result;
    result.as_string = bt_ctf_get_enum_str(def);

    if (bt_ctf_field_get_error() < 0)
      throw std::runtime_error("Error while interpreting string value of enumeration");

    auto inner_def = bt_ctf_get_enum_int(def);
    auto inner_decl = bt_ctf_get_decl_from_def(inner_def);
    result.as_integer = process_integer_field_value(event, inner_def, inner_decl);

    if (bt_ctf_field_get_error() < 0)
      throw std::runtime_error("Error while interpreting integer value of enumeration");

    return result;
  }

  // Tries to extract a std::string from the given def/decl pair.
  // Throws std::runtime_error if there was an error extracting the required data.
  static std::string process_string_field_value(const bt_ctf_event*, const bt_definition* def, const bt_declaration*)
  {
    // TODO(tvoss): Make construction safe here.
    std::string result(bt_ctf_get_string(def));

    if (bt_ctf_field_get_error() < 0)
      throw std::runtime_error("Error while interpreting string value");

    return result;
  }

  // (Recursively) processes the given def/decl pair, returning a ctf::Field::Variant instance containing the resulting values.
  // Throws std::runtime_error in case of issues.
  static ctf::Field::Variant process_field_value(const bt_ctf_event* event, const bt_definition* def, const bt_declaration* decl)
  {
    auto type = bt_ctf_field_type(decl);
    ctf::Field::Variant result;

    switch (type)
    {
      case CTF_TYPE_UNKNOWN:
        break;
      case CTF_TYPE_INTEGER:
        result = process_integer_field_value(event, def, decl);
        break;
      case CTF_TYPE_FLOAT:
        result = process_float_field_value(event, def, decl);
        break;
      case CTF_TYPE_ENUM:
        result = process_integer_field_value(event, def, decl);
        break;
      case CTF_TYPE_STRING:
        result = process_string_field_value(event, def, decl);
        break;
      case CTF_TYPE_STRUCT:
        {
          std::vector<ctf::Field::Variant> v;
          for (uint64_t i = 0; i < bt_ctf_get_struct_field_count(def); i++)
          {
            auto inner_def = bt_ctf_get_struct_field_index(def, i);
            auto inner_decl = bt_ctf_get_decl_from_def(inner_def);
            v.push_back(process_field_value(event, inner_def, inner_decl));
          }
          result = v;
        }
        break;
      case CTF_TYPE_UNTAGGED_VARIANT:
      case CTF_TYPE_VARIANT:
        {
          auto inner_def = bt_ctf_get_variant(def);
          auto inner_decl = bt_ctf_get_decl_from_def(inner_def);
          result = process_field_value(event, inner_def, inner_decl);
          break;
        }
      case CTF_TYPE_ARRAY:
      case CTF_TYPE_SEQUENCE:
        {
          std::vector<ctf::Field::Variant> v;
          unsigned int count(0);
          bt_definition const* const* defs(nullptr);

          if (bt_ctf_get_field_list(event, def, &defs, &count) == 0)
            for(unsigned int i = 0; i < count; i++, defs++)
              v.push_back(process_field_value(event, *defs, bt_ctf_get_decl_from_def(*defs)));

          result = v;
          break;
        }
      default: // Should never be reached.
        break;
    }

    return result;
  }

  // Assembles a ctf::Field value from the given def instance.
  // Throws std::runtime_error in case of issues.
  static ctf::Field process_field_definition(const bt_ctf_event* event, const bt_definition* def)
  {
    auto decl = bt_ctf_get_decl_from_def(def);
    auto type = bt_ctf_field_type(decl);

    return ctf::Field(bt_ctf_field_name(def), static_cast<ctf::Field::Type>(type), process_field_value(event, def, decl));
  }

  // on_new_event is invoked whenever a new event is visited in a trace,
  // just dispatches to the member function of the same name.
  static bt_cb_ret on_new_event(bt_ctf_event* event, void* cookie)
  {
    auto thiz = static_cast<CallbackContext*>(cookie);
    return thiz->on_new_event(event);
  }

  // on_new_event is invoked whenever a new event is visited in a trace,
  // dispatches to the given Enumerator.
  bt_cb_ret on_new_event(bt_ctf_event* event)
  {
    ctf::Event e
    {
      bt_ctf_event_name(event),
      bt_ctf_get_cycles(event),
      std::chrono::nanoseconds{bt_ctf_get_timestamp(event)},
      ctf::Event::Fields{}
    };

    // Iterate over all scopes and read all fields in the respective scope.
    for (ctf::Scope scope : ctf::scopes())
    {
      auto def = bt_ctf_get_top_level_scope(event, static_cast<bt_ctf_scope>(scope));

      unsigned int count(0); bt_definition const* const* defs(nullptr);

      if (bt_ctf_get_field_list(event, def, &defs, &count) == 0)
      {
        for(unsigned int i = 0; i < count; i++, defs++)
        {
          ctf::Event::Key key{scope, bt_ctf_field_name(*defs)};
          e.fields.insert(std::make_pair(key, process_field_definition(event, *defs)));
        }
      }
    }

    // Call out to the enumerator with the assembled event.
    return to_c_api(enumerator(e));
  }

  ctf::Trace::EventEnumerator enumerator;
};
}

/// @brief Returns an iterateable vector of scopes.
const std::vector<ctf::Scope>& ctf::scopes()
{
  static const std::vector<ctf::Scope> instance
  {
    ctf::Scope::trace_packet_header,
    ctf::Scope::stream_packet_context,
    ctf::Scope::stream_event_header,
    ctf::Scope::stream_event_context,
    ctf::Scope::event_context,
    ctf::Scope::event_fields
  };

  return instance;
}

bool ctf::operator<(ctf::Scope lhs, ctf::Scope rhs) noexcept(true)
{
  return static_cast<std::underlying_type<ctf::Scope>::type>(lhs) <
    static_cast<std::underlying_type<ctf::Scope>::type>(rhs);
}

std::ostream& ctf::operator<<(std::ostream& out, ctf::Scope scope)
{
  switch (scope)
  {
    case ctf::Scope::trace_packet_header:
      out << "trace_packet_header"; break;
    case ctf::Scope::stream_packet_context:
      out << "stream_packet_context"; break;
    case ctf::Scope::stream_event_header:
      out << "stream_event_header"; break;
    case ctf::Scope::stream_event_context:
      out << "stream_event_context"; break;
    case ctf::Scope::event_context:
      out << "event_context"; break;
    case ctf::Scope::event_fields:
      out << "event_fields"; break;
  }

  return out;
}

ctf::Integer::Integer()
    : width_(0),
      base_(0),
      value_(Void())
{
}

ctf::Integer::Integer(std::int64_t i, std::uint8_t width, std::uint64_t base)
    : width_(width),
      base_(base),
      value_(i)
{
}

ctf::Integer::Integer(std::uint64_t u, std::uint8_t width, std::uint64_t base)
    : width_(width),
      base_(base),
      value_(u)
{
}

std::uint8_t ctf::Integer::width() const
{
  return width_;
}

std::uint64_t ctf::Integer::base() const
{
  return base_;
}

bool ctf::Integer::is_signed() const
{
  return value_.which() == 1;
}

bool ctf::Integer::is_empty() const
{
  return value_.which() == 0;
}

std::int64_t ctf::Integer::as_int64() const
{
  return boost::get<std::int64_t>(value_);
}

std::uint64_t ctf::Integer::as_uint64() const
{
  return boost::get<std::uint64_t>(value_);
}

ctf::Field::Field(const std::string& name, ctf::Field::Type type, const ctf::Field::Variant& value)
    : name_(name),
      type_(type),
      value_(value)
{
}

ctf::Field::Type ctf::Field::type() const
{
  return type_;
}

bool ctf::Field::is_a(Type type) const
{
  return type_ == type;
}

const std::string& ctf::Field::name() const
{
  return name_;
}

const ctf::Field::Variant& ctf::Field::value() const
{
  return value_;
}

const ctf::Integer& ctf::Field::as_integer() const
{
  return boost::get<ctf::Integer>(value_);
}

double ctf::Field::as_floating_point() const
{
  return boost::get<double>(value_);
}

const ctf::Enumerator& ctf::Field::as_enumerator() const
{
  return boost::get<ctf::Enumerator>(value_);
}

const std::string& ctf::Field::as_string() const
{
  return boost::get<std::string>(value_);
}

const ctf::Field::Variant& ctf::Field::unwrap() const
{
  return boost::get<ctf::Field::Variant>(value_);
}

const std::vector<ctf::Field::Variant>& ctf::Field::as_collection() const
{
  return boost::get<std::vector<ctf::Field::Variant>>(value_);
}

std::ostream& ctf::operator<<(std::ostream& out, const ctf::Integer& integer)
{
  if (integer.is_empty())
    return out << "(empty integer)";

  out << "(w: " << static_cast<std::uint32_t>(integer.width()) << " b: " << integer.base() << " v: ";
  out << std::setbase(integer.base()) << std::showbase;
  if (integer.is_signed())
    out << integer.as_int64();
  else
    out << integer.as_uint64();

  return out << std::dec << std::noshowbase << ")";
}

std::ostream& ctf::operator<<(std::ostream& out, const ctf::Enumerator& enumerator)
{
  return out << enumerator.as_string << " " << enumerator.as_integer;
}

std::ostream& ctf::operator<<(std::ostream& out, const ctf::Field::Type& type)
{
  switch (type)
  {
    case Field::Type::unknown:
      out << "unknown"; break;
    case Field::Type::integer:
      out << "integer"; break;
    case Field::Type::floating_point:
      out << "floating_point"; break;
    case Field::Type::enumeration:
      out << "enumeration"; break;
    case Field::Type::string:
      out << "string"; break;
    case Field::Type::structure:
      out << "structure"; break;
    case Field::Type::untagged_variant:
      out << "untagged_variant"; break;
    case Field::Type::variant:
      out << "variant"; break;
    case Field::Type::array:
      out << "array"; break;
    case Field::Type::sequence:
      out << "sequence"; break;
  }
  return out;
}

std::ostream& ctf::operator<<(std::ostream& out, const ctf::Void&)
{
  return out;
}

namespace
{
struct PrintVisitor : public boost::static_visitor<std::ostream&>
{
  PrintVisitor(std::ostream& out) : out(out)
  {
  }

  template<typename T>
  std::ostream& operator()(const T& value) const
  {
    return out << value;
  }

  std::ostream& out;
};
}

std::ostream& ctf::operator<<(std::ostream& out, const ctf::Field::Variant& variant)
{
  PrintVisitor pv{out};
  return boost::apply_visitor(pv, variant);
}

std::ostream& ctf::operator<<(std::ostream& out, const ctf::Field& field)
{
  return out << "[" << field.name() << " " << field.type() << " " << field.value() << "]";
}

std::ostream& ctf::operator<<(std::ostream& out, const ctf::Event& event)
{
  static constexpr const std::size_t idx_scope{0};

  out << "{" << "\n"
      << "  " << event.name << "\n"
      << "  " << event.cycles << " [cycles]" << "\n"
      << "  " << event.timestamp.count() << " [ns]" << "\n";

  for (const auto& field : event.fields)
    out << "    " << std::get<idx_scope>(field.first) << " -> " << field.second << "\n";

  return out << "}";
}

namespace
{
// Finds the first directory on the given path or below that contains a folder "metadata".
// We take that as an indication for: Contains a ctf trace.
boost::filesystem::path find_directory_with_meta_data(const boost::filesystem::path& path)
{
  static constexpr const char* metadata("metadata");

  if (boost::filesystem::exists(path / metadata))
    return path;

  boost::filesystem::recursive_directory_iterator it(path), itE;

  while (it != itE)
  {
    if (boost::filesystem::exists(*it / metadata))
      return *it;

    ++it;
  }

  throw std::runtime_error("Could not find a ctf trace");
}

// Some handy constants that we pass in to bt_context_add_trace on construction.
typedef void (*packet_seek)(struct bt_stream_pos *pos, size_t index, int whence);
packet_seek the_empty_seek_function(nullptr);
bt_mmap_stream_list* the_empty_stream_list(nullptr);
FILE* the_empty_metadata_file(nullptr);
}

ctf::Trace::Trace(const boost::filesystem::path& path)
    : path_(find_directory_with_meta_data(path)),
      context(bt_context_create()),
      trace_handle(bt_context_add_trace(context, path_.c_str(), "ctf", the_empty_seek_function, the_empty_stream_list, the_empty_metadata_file))
{
}

void ctf::Trace::for_each_event(ctf::Trace::EventEnumerator enumerator)
{
  static bt_dependencies* the_empty_dependencies(nullptr);
  static const bt_iter_pos* begin(nullptr);
  static const bt_iter_pos* end(nullptr);
  static const bt_intern_str call_back_for_all_events(0);
  static const int the_empty_flags(0);

  CallbackContext cb_context{enumerator};

  bt_ctf_iter* it = bt_ctf_iter_create(context, begin, end);
  bt_ctf_iter_add_callback(
      it,
      call_back_for_all_events,
      &cb_context,
      the_empty_flags,
      CallbackContext::on_new_event,
      the_empty_dependencies,
      the_empty_dependencies,
      the_empty_dependencies);

  // Walk all traces in the context until there are no more events.
  bt_ctf_event* ctf_event(nullptr);
  while ((ctf_event = bt_ctf_iter_read_event(it))) {
    if (bt_iter_next(bt_ctf_get_iter(it)) < 0)
      break;
  }
}
