#include <condition_variable>

#include <elle/assert.hh>
#include <elle/meta.hh>
#include <elle/reactor/Barrier.hh>
#include <elle/reactor/exception.hh>
#include <elle/reactor/signal.hh>
#include <elle/reactor/Thread.hh>

namespace elle
{
  namespace reactor
  {
    /*----------------.
    | Multithread API |
    `----------------*/

    template <typename R>
    R
    Scheduler::mt_run(const std::string& name,
                      const std::function<R ()>& action)
    {
      ELLE_ASSERT(!this->done());
      R res;
      std::mutex mutex;
      std::condition_variable cond;
      std::exception_ptr exn;
      {
        std::unique_lock<std::mutex> lock(mutex);
        this->run_later(name, [&, action]
                        {
                          try
                          {
                            res = action();
                          }
                          catch (Terminate const&)
                          {
                            // Ignore
                          }
                          catch (...)
                          {
                            exn = std::current_exception();
                          }
                          std::unique_lock<std::mutex> lock(mutex);
                          cond.notify_one();
                        });
        cond.wait(lock);
        if (exn)
          std::rethrow_exception(exn);
      }
      return res;
    }

    template <>
    void
    Scheduler::mt_run<void>(const std::string& name,
                            const std::function<void ()>& action);

    class Waiter
      : public Barrier
    {
    public:
      template <typename ... Prototype, typename F>
      Waiter(boost::signals2::signal<void (Prototype...)>& signal,
             F predicate)
        : _connection(
          signal.connect(
            [this, pred = std::move(predicate)] (Prototype ... args)
            {
              if (pred(args...))
                this->open();
            }))
      {}

      ELLE_ATTRIBUTE(boost::signals2::scoped_connection, connection);
    };

    namespace _details
    {
      /// Waiting for a predicate.
      template <typename R, typename ... Prototype, typename F,
                typename = std::enable_if_t<
                  std::is_convertible<F, std::function<bool (Prototype ...)>>::value>>
      Waiter
      waiter(boost::signals2::signal<R (Prototype...)>& signal,
             F predicate)
      {
        return {signal, std::move(predicate)};
      }

      /// Waiting for a value.
      template <typename... Prototype, typename... Args,
                typename = std::enable_if_t<
                  !std::is_convertible<
                    typename elle::meta::List<Args...>::template head<void>::type,
                    std::function<bool (Prototype ...)>>::value>>
      Waiter
      waiter(boost::signals2::signal<void (Prototype...)>& signal,
             Args... values)
      {
        // GCC 4.8 work-around: [vals = std::make_tuple(std::move(values)...)]
        // fails to compile.
        auto vals = std::make_tuple(std::move(values)...);
        return {
          signal,
          [vals = std::move(vals)] (Prototype... args)
          {
            return vals == std::forward_as_tuple(args...);
          }};
      }
    }

    template <typename Prototype, typename ... Args>
    Waiter
    waiter(boost::signals2::signal<Prototype>& signal, Args&&... args)
    {
      return _details::waiter(signal, std::forward<Args>(args)...);
    }

    template <typename Prototype, typename ... Args>
    void
    wait(boost::signals2::signal<Prototype>& signal, Args&& ... args)
    {
      auto w = waiter(signal, std::forward<Args>(args)...);
      reactor::wait(w);
    }

    template <typename Fun>
    void
    run(bool later, std::string const& name, Fun f)
    {
      if (later)
        run_later(name, f);
      else
        f();
    }
  }
}
