#pragma once

#include <das/cli.hh>

#include <infinit/cli/Object.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

/// Whether to enable Docker support.
#if !defined INFINIT_PRODUCTION_BUILD || defined INFINIT_LINUX
# define WITH_DOCKER
#endif

namespace infinit
{
  namespace cli
  {
    class Daemon
      : public Object<Daemon>
    {
    public:
      Daemon(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::disable_storage,
                                    cli::enable_storage,
                                    cli::fetch,
                                    cli::run,
                                    cli::start,
                                    cli::status,
                                    cli::stop,
                                    cli::manage_volumes));
      using Strings = std::vector<std::string>;

      /*------------------------.
      | Mode: disable_storage.  |
      `------------------------*/
      Mode<Daemon,
           decltype(modes::mode_disable_storage),
           decltype(cli::name)>
      disable_storage;
      void
      mode_disable_storage(std::string const& name);


      /*-----------------------.
      | Mode: enable_storage.  |
      `-----------------------*/
      Mode<Daemon,
           decltype(modes::mode_enable_storage),
           decltype(cli::name),
           decltype(cli::hold)>
      enable_storage;
      void
      mode_enable_storage(std::string const& name,
                          bool hold);


      /*--------------.
      | Mode: fetch.  |
      `--------------*/
      Mode<Daemon,
           decltype(modes::mode_fetch),
           decltype(cli::name)>
      fetch;
      void
      mode_fetch(std::string const& name);

      /*-----------------------.
      | Mode: manage_volumes.  |
      `-----------------------*/
      Mode<Daemon,
           decltype(modes::mode_manage_volumes),
           decltype(cli::list = false),
           decltype(cli::status = false),
           decltype(cli::start = false),
           decltype(cli::stop = false),
           decltype(cli::restart = false),
           decltype(cli::name = boost::none)>
      manage_volumes;
      void
      mode_manage_volumes(bool list = false,
                          bool status = false,
                          bool start = false,
                          bool stop = false,
                          bool restart = false,
                          boost::optional<std::string> const& name = {});

      /*------------.
      | Mode: run.  |
      `------------*/
      Mode<Daemon,
           decltype(modes::mode_run),
           decltype(cli::login_user = Strings{}),
           decltype(cli::mount = Strings{}),
           decltype(cli::mount_root = boost::none),
           decltype(cli::default_network = boost::none),
           decltype(cli::advertise_host = Strings{}),
           decltype(cli::fetch = false),
           decltype(cli::push = false),
#ifdef WITH_DOCKER
           decltype(cli::docker = true),
           decltype(cli::docker_user = boost::none),
           decltype(cli::docker_home = boost::none),
           decltype(cli::docker_socket_tcp = false),
           decltype(cli::docker_socket_port = 0),
           decltype(cli::docker_socket_path = "/run/docker/plugins"),
           decltype(cli::docker_descriptor_path = "/usr/lib/docker/plugins"),
           decltype(cli::docker_mount_substitute = ""),
#endif
           decltype(cli::log_level = boost::none),
           decltype(cli::log_path = boost::none)>
      run;
      void
      mode_run(Strings const& login_user,
               Strings const& mount,
               boost::optional<std::string> const& mount_root,
               boost::optional<std::string> const& default_network,
               Strings const& advertise_host,
               bool fetch,
               bool push,
#ifdef WITH_DOCKER
               bool docker,
               boost::optional<std::string> const& docker_user,
               boost::optional<std::string> const& docker_home,
               bool docker_socket_tcp,
               int const& docker_socket_port,
               std::string const& docker_socket_path,
               std::string const& docker_descriptor_path,
               std::string const& docker_mount_substitute,
#endif
               boost::optional<std::string> const& log_level,
               boost::optional<std::string> const& log_path);

      /*--------------.
      | Mode: start.  |
      `--------------*/
      Mode<Daemon,
           decltype(modes::mode_start),
           decltype(cli::login_user = Strings{}),
           decltype(cli::mount = Strings{}),
           decltype(cli::mount_root = boost::none),
           decltype(cli::default_network = boost::none),
           decltype(cli::advertise_host = Strings{}),
           decltype(cli::fetch = false),
           decltype(cli::push = false),
#ifdef WITH_DOCKER
           decltype(cli::docker = true),
           decltype(cli::docker_user = boost::none),
           decltype(cli::docker_home = boost::none),
           decltype(cli::docker_socket_tcp = false),
           decltype(cli::docker_socket_port = 0),
           decltype(cli::docker_socket_path = "/run/docker/plugins"),
           decltype(cli::docker_descriptor_path = "/usr/lib/docker/plugins"),
           decltype(cli::docker_mount_substitute = ""),
#endif
           decltype(cli::log_level = boost::none),
           decltype(cli::log_path = boost::none)>
      start;
      void
      mode_start(Strings const& login_user,
                 Strings const& mount,
                 boost::optional<std::string> const& mount_root,
                 boost::optional<std::string> const& default_network,
                 Strings const& advertise_host,
                 bool fetch,
                 bool push,
#ifdef WITH_DOCKER
                 bool docker,
                 boost::optional<std::string> const& docker_user,
                 boost::optional<std::string> const& docker_home,
                 bool docker_socket_tcp,
                 int const& docker_socket_port,
                 std::string const& docker_socket_path,
                 std::string const& docker_descriptor_path,
                 std::string const& docker_mount_substitute,
#endif
                 boost::optional<std::string> const& log_level,
                 boost::optional<std::string> const& log_path);


      /*---------------.
      | Mode: status.  |
      `---------------*/
      Mode<Daemon,
           decltype(modes::mode_status)>
      status;
      void
      mode_status();

      /*-------------.
      | Mode: stop.  |
      `-------------*/
      Mode<Daemon,
           decltype(modes::mode_stop)>
      stop;
      void
      mode_stop();
    };
  }
}