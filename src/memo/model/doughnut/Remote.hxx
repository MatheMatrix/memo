#include <elle/chrono.hh>
#include <elle/os/environ.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      template<typename F, typename ...Args>
      typename RPC<F>::result_type
      remote_call_next(RemoteRPC<F>* ptr, Args const& ... args)
      {
        return ptr->RPC<F>::operator()(args...);
      }

      template<typename F>
      template<typename ...Args>
      typename RPC<F>::result_type
      RemoteRPC<F>::operator()(Args const& ... args)
      {
        // GCC bug, argument packs dont work in lambdas
        auto helper = std::bind(&remote_call_next<F, Args...>,
          this, std::ref(args)...);
        return _remote->safe_perform(
          this->name(),
          [&]
          {
            ELLE_LOG_COMPONENT("memo.model.doughnut.Remote");
            // Hold a reference to the connection, in case we
            // disconnect/reconnect concurrently.
            auto connection = this->_remote->_connection;
            this->_channels = connection->channels().get();
            auto creds = _remote->credentials();
            if (!creds.empty())
            {
              elle::Buffer c(creds);
              this->key().emplace(std::move(c));
            }
            return helper();
        });
      }

      template <typename Op>
      auto
      Remote::safe_perform(std::string const& name, Op op)
        -> decltype(op())
      {
        ELLE_LOG_COMPONENT("memo.model.doughnut.Remote");
        auto const rpc_timeout = this->doughnut().connect_timeout();
        auto const soft_fail = this->doughnut().soft_fail_timeout();
        auto const rpc_start = std::chrono::system_clock::now();
        auto const disconnected_for =
          rpc_start - this->_connection->disconnected_since();
        // No matter what, if we are disconnected, retry.
        if (this->_connection->disconnected())
        {
          ELLE_TRACE("%s: reconnect before running \"%s\"", this, name);
          this->reconnect();
        }
        // If we exceeded the connection time, retry.
        else if (!this->_connection->connected() &&
                 disconnected_for >= rpc_timeout)
        {
          ELLE_TRACE("%s: drop stale connection before running \"%s\"",
                     this, name);
          this->reconnect();
        }
        bool retry = true;
        while (true)
        {
          auto const rpc_timeout_delay =
            rpc_timeout - (std::chrono::system_clock::now() - rpc_start);
          auto const disconnected_for =
            std::chrono::system_clock::now() - this->_connecting_since;
          auto const soft_fail_delay = soft_fail - disconnected_for;
          try
          {
            // Try connecting until we reach the RPC timeout or the Remote
            // softfail.
            auto const delay = std::min(rpc_timeout_delay, soft_fail_delay);
            if (elle::reactor::wait(this->_connected, delay))
            {
              auto const delay = rpc_timeout - (elle::Clock::now() - rpc_start);
              auto&& timeout = elle::reactor::AsioTimer(
                elle::reactor::scheduler().io_service(), delay);
              if (this->doughnut().soft_fail_running())
                timeout.async_wait(
                  [&, this, t = elle::reactor::scheduler().current()]
                  (boost::system::error_code const& e)
                  {
                    if (!e)
                    {
                      ELLE_TRACE("%s: soft timeout on \"%s\" after %s",
                                 this, name, delay);
                      this->_disconnected_exception =
                        std::make_exception_ptr(elle::reactor::network::TimeOut());
                      t->raise_and_wake<elle::reactor::network::TimeOut>();
                      retry = false;
                    }
                  });
              ELLE_TRACE("%s: run \"%s\"", this, name)
                return op();
            }
            else
            {
              ELLE_TRACE("%s: soft-fail running \"%s\"", this, name);
              retry = false;
            }
          }
          catch (elle::reactor::network::Error const& e)
          {
            ELLE_TRACE("%s: network exception when invoking %s: %s",
                       this, name, e);
          }
          catch (elle::protocol::Serializer::EOS const& e)
          {
            ELLE_TRACE("%s: end of stream when invoking %s: %s", this, name, e);
          }
          catch (elle::Error const& e)
          {
            ELLE_TRACE("%s: connection error: %s", this, e);
            throw;
          }
          if (retry)
            this->reconnect();
          else if (rpc_timeout_delay < soft_fail_delay)
          {
            ELLE_TRACE("%s: give up rpc %s after %s",
                       this, name, rpc_timeout);
            throw elle::reactor::network::TimeOut();
          }
          else
          {
            ELLE_TRACE(
              "%s: soft-fail rpc %s after remote was disconnected for %s",
              this, name, disconnected_for);
            ELLE_ASSERT(this->_disconnected_exception);
            std::rethrow_exception(this->_disconnected_exception);
          }
        }
      }

      template <typename F>
      RemoteRPC<F>::RemoteRPC(std::string name, Remote* remote)
        : Super{std::move(name),
                remote->_connection->channels().get(),
                remote->doughnut().version(),
                elle::unconst(&remote->credentials())}
        , _remote(remote)
      {
        this->template set_context<Remote*>(remote);
      }

      template <typename F>
      RemoteRPC<F>
      Remote::make_rpc(std::string const& name)
      {
        return RemoteRPC<F>(name, this);
      }
    }
  }
}
