#include <type_traits>

#include <elle/Backtrace.hh>
#include <elle/log.hh>
#include <elle/printf.hh>
#include <elle/memory.hh>

#include <elle/reactor/network/Error.hh>
#include <elle/reactor/Scope.hh>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Thread.hh>

#include <elle/protocol/Channel.hh>
#include <elle/protocol/ChanneledStream.hh>
#include <elle/protocol/exceptions.hh>

namespace elle
{
  namespace protocol
  {

    inline LastMessageException::LastMessageException(std::string const& what)
      : elle::Exception(what)
    {}

    /*--------------.
    | BaseProcedure |
    `--------------*/

    template <typename IS, typename OS>
    BaseProcedure<IS, OS>::
    BaseProcedure(std::string const& name)
      : _name(name)
    {}

    template <typename IS, typename OS>
    BaseProcedure<IS, OS>::~BaseProcedure()
    {}

    /*----------.
    | Procedure |
    `----------*/

    template <typename IS,
              typename OS,
              typename R,
              typename ... Args>
    Procedure<IS, OS, R, Args ...>::
    Procedure (std::string const& name,
               RPC<IS, OS>& owner,
               uint32_t id,
               std::function<R (Args...)> const& f)
      : BaseProcedure<IS, OS>(name)
      , _id(id)
      , _owner(owner)
      , _function(f)
    {}

    template <typename IS,
              typename OS,
              typename R,
              typename ... Args>
    Procedure<IS, OS, R, Args ...>::~Procedure()
    {}

    /*------------------------.
    | RemoteProcedure helpers |
    `------------------------*/

    template <typename OS>
    static
    void
    put_args(OS&)
    {}

    template <typename OS,
              typename T,
              typename ...Args>
    static
    void
    put_args(OS& output, T a, Args ... args)
    {
      output << a;
      put_args<OS, Args...>(output, args...);
    }

    template <typename IS,
              typename R>
    struct GetRes
    {
      static inline R get_res(IS& input)
      {
        R res;
        input >> res;
        return res;
      }
    };

    template <typename IS>
    struct GetRes<IS, void>
    {
      static
      void
      get_res(IS& input)
      {
        char c;
        input >> c;
      }
    };

    /*----------------.
    | RemoteProcedure |
    `----------------*/

    template <typename IS,
              typename OS>
    template <typename R,
              typename ... Args>
    RPC<IS, OS>::RemoteProcedure<R, Args ...>::RemoteProcedure(
      std::string const& name,
      RPC<IS, OS>& owner)
      : RemoteProcedure<R, Args...>(owner.add<R, Args...>(name))
    {}

    template <typename IS,
              typename OS>
    template <typename R,
              typename ... Args>
    RPC<IS, OS>::RemoteProcedure<R, Args ...>:: RemoteProcedure(
      std::string const& name,
      RPC<IS, OS>& owner,
      uint32_t id)
      : _id(id)
      , _name(name)
      , _owner(owner)
    {}

    template <typename IS,
              typename OS>
    template <typename R,
              typename ... Args>
    void
    RPC<IS, OS>::RemoteProcedure<R, Args ...>::
    operator = (std::function<R (Args...)> const& f)
    {
      auto proc = this->_owner._procedures.find(this->_id);
      assert(proc != this->_owner._procedures.end());
      assert(proc->second.second == nullptr);
      proc->second.second =
        std::make_unique<Procedure<IS, OS, R, Args...>>(
          this->_name, this->_owner, this->_id, f);
    }


    template <typename IS,
              typename OS>
    template <typename R,
              typename ... Args>
    R
    RPC<IS, OS>::RemoteProcedure<R, Args...>::
    operator () (Args ... args)
    {
      ELLE_LOG_COMPONENT("elle.protocol.RPC");

      ELLE_TRACE_SCOPE("%s: call remote procedure: %s",
                       this->_owner, this->_name);

      Channel channel(this->_owner._channels);
      {
        elle::Buffer question;
        {
          elle::IOStream outs(question.ostreambuf());
          OS output(outs);
          output << this->_id;
          put_args<OS, Args...>(output, args...);
        }
        channel.write(question);
      }
      {
        elle::Buffer response(channel.read());
        elle::IOStream ins(response.istreambuf());
        IS input(ins);
        bool res;
        input >> res;
        if (res)
          return GetRes<IS, R>::get_res(input);
        else
        {
          auto error = input_error(input);
          ELLE_TRACE_SCOPE("%s: remote procedure call failed: %s",
                           this->_owner, error.what());
          // FIXME: only protocol error should throw this, not remote
          // exceptions.
          auto e =
            RPCError(sprintf("remote procedure '%s' failed with '%s'",
                             this->_name, error.what()));
          e.inner_exception(std::make_exception_ptr(error));
          throw e;
        }
      }
    }

    /*------------------.
    | Procedure helpers |
    `------------------*/

    template <typename Input,
              typename R,
              typename ... Types>
    struct Call;

    template <typename Input,
              typename R>
    struct Call<Input, R>
    {
      template <typename S, typename ... Given>
      static
      R
      call(Input&,
           S const& f,
           Given&... args)
      {
        return f(args...);
      }
    };

    template <typename Input,
              typename R,
              typename First,
              typename ... Types>
    struct Call<Input, R, First, Types...>
    {
      template <typename S,
                typename ... Given>
      static
      R
      call(Input& input,
           S const& f,
           Given&&... args)
      {
        auto a = std::remove_const_t<std::remove_reference_t<First>>{};
        input >> a;
        return Call<Input, R, Types...>::call(input, f,
                                              std::forward<Given>(args)..., a);
      }
    };

    namespace
    {
      template <typename IS,
                typename OS,
                typename R,
                typename ... Args>
      struct VoidSwitch
      {
        static
        void
        call(IS& in,
             OS& out,
             std::function<R (Args...)> const& f)
        {
          R res(Call<IS, R, Args...>::template call<>(in, f));
          out << true;
          out << res;
        }
      };

      template <typename IS,
                typename OS,
                typename ... Args>
      struct VoidSwitch<IS, OS, void, Args ...>
      {
        static
        void
        call(IS& in,
             OS& out,
             std::function<void (Args...)> const& f)
        {
          Call<IS, void, Args...>::template call<>(in, f);
          out << true;
          unsigned char c(42);
          out << c;
        }
      };
    }

    /*----------.
    | Procedure |
    `----------*/

    template <typename IS,
              typename OS,
              typename R,
              typename ... Args>
    void
    Procedure<IS, OS, R, Args...>::_call(IS& in, OS& out)
    {
      std::string err;
      VoidSwitch<IS, OS, R, Args ...>::call(
        in, out, this->_function);
    }

    /*----.
    | RPC |
    `----*/

    template <typename IS,
              typename OS>
    template <typename R,
              typename ... Args>
    typename RPC<IS, OS>::template RemoteProcedure<R, Args...>
    RPC<IS, OS>::add(std::function<R (Args...)> const& f)
    {
      uint32_t id = this->_id++;
      using Proc = Procedure<IS, OS, R, Args...>;
      this->_procedures[id] = std::make_unique<Proc>(*this, id, f);
      return {*this, id};
    }

    template <typename IS,
              typename OS>
    template <typename R,
              typename ... Args>
    typename RPC<IS, OS>::template RemoteProcedure<R, Args...>
    RPC<IS, OS>::add(std::string const& name)
    {
      uint32_t id = this->_id++;
      this->_procedures[id] = NamedProcedure(name, nullptr);
      return {name, *this, id};
    }

    template <typename IS,
              typename OS>
    RPC<IS, OS>::RPC(ChanneledStream& channels)
      : BaseRPC(channels)
    {}

    // FIXME: move closer to elle::Error.
    namespace
    {
      template <typename OStream>
      void
      output_error(OStream& os, elle::Error const& e)
      {
        os << std::string(e.what());
        os << uint16_t(e.backtrace().frames().size());
        for (auto const& frame: e.backtrace().frames())
          os << frame.symbol
             << frame.symbol_mangled
             << frame.symbol_demangled
             << frame.address
             << frame.offset;
      }

      template <typename IStream>
      elle::Error
      input_error(IStream& is)
      {
        std::string err;
        is >> err;
        uint16_t size;
        is >> size;
        auto frames = std::vector<StackFrame>{};
        for (int i = 0; i < size; ++i)
        {
          frames.emplace_back();
          auto& frame = frames.back();
          is >> frame.symbol
             >> frame.symbol_mangled
             >> frame.symbol_demangled
             >> frame.address
             >> frame.offset;
        }
        return {std::move(frames), std::move(err)};
      }
    }

    template<typename T>
    bool
    handle_exception(ExceptionHandler & handler,
                     T& output,
                     std::exception_ptr ex)
    {
      ELLE_LOG_COMPONENT("elle.protocol.RPC");
      bool res = false;
      try
      {
        // would be nice if handler could reply a value
        if (handler)
          handler(std::current_exception());
        std::rethrow_exception(ex);
      }
      catch (elle::Exception& e)
      {
        if (dynamic_cast<LastMessageException*>(&e))
          res = true;
        ELLE_TRACE_SCOPE("RPC procedure failed: %s (stop_request = %s)",
          e.what(), res);
        output << false;
        output_error(output, e);
      }
      catch (std::exception& e)
      {
        ELLE_TRACE_SCOPE("RPC procedure failed: %s", e.what());
        output << false
               << std::string(e.what())
               << uint16_t(0);
      }
      catch (...)
      {
        ELLE_TRACE_SCOPE("RPC procedure failed: unknown error");
        output << false
               << std::string("unknown error")
               << uint16_t(0);
      }
      return res;
    }

    template <typename IS,
              typename OS>
    void
    RPC<IS, OS>::run(ExceptionHandler handler)
    {
      ELLE_LOG_COMPONENT("elle.protocol.RPC");

      using elle::sprintf;
      using elle::Exception;
      bool stop_request = false;
      try
      {
        while (!stop_request)
        {
          ELLE_TRACE_SCOPE("%s: Accepting new request...", *this);
          Channel c(this->_channels.accept());
          elle::Buffer question(c.read());
          elle::IOStream ins(question.istreambuf());
          IS input(ins);
          uint32_t id;
          input >> id;
          ELLE_TRACE_SCOPE("%s: Processing request for %s...", *this, id);
          auto proc = this->_procedures.find(id);

          elle::Buffer answer;
          elle::IOStream outs(answer.ostreambuf());
          OS output(outs);
          try
          {
            if (proc == this->_procedures.end())
              throw Exception(sprintf("call to unknown procedure: %s", id));
            else if (proc->second.second == nullptr)
            {
              throw Exception(sprintf("remote call to non-local procedure: %s",
                                      proc->second.first));
            }
            else
            {
              auto const &name = proc->second.first;

              ELLE_TRACE("%s: remote procedure called: %s", *this, name)
                proc->second.second->_call(input, output);
              ELLE_TRACE("%s: procedure %s succeeded", *this, name);
            }
          }
          catch (elle::reactor::Terminate const&)
          {
            ELLE_TRACE("%s: terminating as requested", *this);
            throw;
          }
          catch (...)
          { // Pass exception through handler if present, reply with an error
            stop_request = handle_exception(handler, output, std::current_exception());
          }
          outs.flush();
          c.write(answer);
        }
      }
      catch (elle::reactor::network::ConnectionClosed const& e)
      {
        ELLE_TRACE("%s: end of RPCs: connection closed", *this);
        return;
      }
      ELLE_TRACE("%s: end of RPCs: normal exit", *this);
    }

    // XXX: factor with run().
    // XXX: unsafe, rpc calls must finish in the order they were started,
    // there is no mechanism to match a rpc call with associated return
    template <typename IS,
              typename OS>
    void
    RPC<IS, OS>::parallel_run()
    {
      ELLE_LOG_COMPONENT("elle.protocol.RPC");

      using elle::sprintf;
      using elle::Exception;
      try
      {
        elle::With<elle::reactor::Scope>("RPC // run") << [&] (elle::reactor::Scope& scope)
        {
          int i = 0;
          while (true)
          {
            auto chan = std::make_shared<Channel>(this->_channels.accept());
            ++i;

            auto call_procedure = [&, chan] {
              ELLE_LOG_COMPONENT("elle.protocol.RPC");

              elle::Buffer question(chan->read());
              elle::IOStream ins(question.istreambuf());
              IS input(ins);
              uint32_t id;
              input >> id;
              auto proc = this->_procedures.find(id);

              elle::Buffer answer;
              elle::IOStream outs(answer.ostreambuf());
              OS output(outs);
              try
              {
                if (proc == _procedures.end())
                  throw Exception(sprintf("call to unknown procedure: %s", id));
                else if (proc->second.second == nullptr)
                  throw Exception(sprintf("remote call to non-local procedure: %s",
                                          proc->second.first));
                else
                {
                  auto const &name = proc->second.first;
                  ELLE_TRACE("%s: remote procedure called: %s", *this, name)
                    proc->second.second->_call(input, output);
                  ELLE_TRACE("%s: procedure %s succeeded", *this, name);
                }
              }
              catch (elle::Error const& e)
              {
                ELLE_TRACE("%s: procedure failed: %s", *this, e.what());
                output << false;
                output_error(output, e);
              }
              catch (elle::reactor::Terminate const&)
              {
                ELLE_TRACE("%s: terminating as requested", *this);
                throw;
              }
              catch (std::exception& e)
              {
                ELLE_TRACE("%s: procedure failed: %s", *this, e.what());
                output << false
                       << std::string(e.what())
                       << uint16_t(0);
              }
              catch (...)
              {
                ELLE_TRACE("%s: procedure failed: unknown error", *this);
                output << false
                       << std::string("unknown error")
                       << uint16_t(0);
              }
              outs.flush();
              chan->write(answer);
            };
            scope.run_background(elle::sprintf("RPC %s", i), call_procedure);
          }
        };
      }
      catch (elle::reactor::network::ConnectionClosed const& e)
      {
        ELLE_TRACE("%s: end of RPCs: connection closed", *this);
        return;
      }
      catch (elle::Exception& e)
      {
        ELLE_WARN("%s: end of RPCs: %s", *this, e);
        throw;
      }
      catch (std::exception& e)
      {
        ELLE_WARN("%s: end of RPCs: %s", *this, e.what());
        throw;
      }
      catch (...)
      {
        ELLE_WARN("%s: end of RPCs: unkown error", *this);
        throw;
      }
    }

    template <typename IS,
              typename OS>
    void
    RPC<IS, OS>::add(BaseRPC& rpc)
    {
      this->_rpcs.push_back(&rpc);
    }

    template <typename IS,
              typename OS>
    void
    RPC<IS, OS>::print(std::ostream& stream) const
    {
      // FIXME: mitigate
      stream << "RPC pool";
    }
  }
}
