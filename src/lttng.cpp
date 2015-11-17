#include <lttng/lttng.h>
#include <lttng/ctf.h>

#include <core/posix/exec.h>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace
{
std::map<std::string, std::string> copy_env()
{
    std::map<std::string, std::string> result;
    core::posix::this_process::env::for_each([&result](const std::string& s, const std::string& t)
    {
        result.insert(std::make_pair(s, t));
    });
    return result;
}

void throw_if_error(const core::posix::wait::Result& result)
{
    if (result.status != core::posix::wait::Result::Status::exited)
        throw std::runtime_error("The lttng executable did not exit");

    if (result.detail.if_exited.status != core::posix::exit::Status::success)
        throw std::runtime_error("The lttng executable exited with an error.");
}
}

std::ostream& lttng::operator<<(std::ostream& out, lttng::Domain domain)
{
    switch (domain)
    {
    case lttng::Domain::kernel: out << "kernel"; break;
    case lttng::Domain::userspace: out << "userspace"; break;
    }

    return out;
}

std::ostream& lttng::operator<<(std::ostream& out, lttng::Context context)
{
    switch (context)
    {
    case lttng::Context::pid: out << "pid"; break;
    case lttng::Context::proc_name: out << "procname"; break;
    case lttng::Context::prio: out << "prio"; break;
    case lttng::Context::nice: out << "nice"; break;
    case lttng::Context::vpid: out << "vpid"; break;
    case lttng::Context::tid: out << "tid"; break;
    case lttng::Context::vtid: out << "vtid"; break;
    case lttng::Context::ppid: out << "ppid"; break;
    case lttng::Context::vppid: out << "vppid"; break;
    case lttng::Context::pthread_id: out << "pthread_id"; break;
    case lttng::Context::hostname: out << "hostname"; break;
    case lttng::Context::ip: out << "ip"; break;
    }

    return out;
}

lttng::Exception::Exception(int code) noexcept(true)
    : std::runtime_error(""),
      code(code)
{
}

std::unique_ptr<lttng::Tracer> lttng::Tracer::create(lttng::Domain domain)
{
    return std::unique_ptr<lttng::Tracer>(new lttng::Tracer(domain));
}

lttng::Tracer::Tracer(lttng::Domain domain) : domain{domain}
{
}

std::shared_ptr<lttng::Session> lttng::Tracer::create_session(const std::string& name, const std::shared_ptr<lttng::Consumer>& consumer)
{
    return std::make_shared<lttng::Session>(domain, name, consumer);
}

lttng::FileSystemConsumer::FileSystemConsumer(const boost::filesystem::path& path) : path_(path)
{
    boost::system::error_code ec; boost::filesystem::create_directories(path_, ec);
    if (ec)
    {
        std::stringstream ss; ss << "FileSystemConsumer::FileSystemConsumer could not create path " << path_ << ": " << ec.message();
        throw std::runtime_error{ss.str()};
    }
}

const boost::filesystem::path& lttng::FileSystemConsumer::path() const
{
    return path_;
}

std::string lttng::FileSystemConsumer::to_url() const
{
    return "file://" + path_.native();
}

lttng::Session::Session(lttng::Domain domain, const std::string& name, const std::shared_ptr<lttng::Consumer>& consumer) 
    : domain_(domain),
      name_(name),
      consumer_(consumer)
{
    auto cp = core::posix::exec(
                "/usr/bin/lttng", {"create", name_, "--set-url", consumer->to_url()},
                copy_env(), core::posix::StandardStream::empty);

    throw_if_error(cp.wait_for(core::posix::wait::Flags::untraced));
}

lttng::Session::~Session()
{
    auto cp = core::posix::exec(
                "/usr/bin/lttng", {"destroy", name_},
                copy_env(), core::posix::StandardStream::empty);

    throw_if_error(cp.wait_for(core::posix::wait::Flags::untraced));
}

const std::string& lttng::Session::name() const
{
    return name_;
}

void lttng::Session::add_context(lttng::Context context)
{
    auto cp = core::posix::exec(
                "/usr/bin/lttng", {"add-context", "-t", boost::lexical_cast<std::string>(context), "--" + boost::lexical_cast<std::string>(domain_), "-s", name_},
                copy_env(), core::posix::StandardStream::empty);

    throw_if_error(cp.wait_for(core::posix::wait::Flags::untraced));
}

void lttng::Session::enable_event(const std::string& event)
{
    auto cp = core::posix::exec(
                "/usr/bin/lttng", {"enable-event", event, "--" + boost::lexical_cast<std::string>(domain_), "-s", name_},
                copy_env(), core::posix::StandardStream::empty);
    throw_if_error(cp.wait_for(core::posix::wait::Flags::untraced));
}

void lttng::Session::start()
{
    auto cp = core::posix::exec(
                "/usr/bin/lttng", {"start", name_},
                copy_env(), core::posix::StandardStream::empty);

    throw_if_error(cp.wait_for(core::posix::wait::Flags::untraced));
}

void lttng::Session::stop()
{
    auto cp = core::posix::exec(
                "/usr/bin/lttng", {"stop", name_},
                copy_env(), core::posix::StandardStream::stdout);

    throw_if_error(cp.wait_for(core::posix::wait::Flags::untraced));
}
