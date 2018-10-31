#include <elle/log.hh>

namespace elle
{
  namespace reactor
  {
    /*-------------.
    | Construction |
    `-------------*/

    template <typename ... Args>
    Thread::Thread(std::string const& name,
                   Action action,
                   Args&& ... args)
      : Thread(name, std::move(action))
    {
      elle::das::named::prototype(reactor::dispose = false,
                                  reactor::managed = false)
        .call([&] (bool dispose, bool managed)
              {
                this->_dispose = dispose;
                this->_managed = managed;
              }, std::forward<Args>(args)...);
    }

    template <typename R>
    static
    void
    vthread_catcher(typename VThread<R>::Action const& action,
                    R& result)
    {
      result = action();
    }

    template <typename Exception, typename... Args>
    void
    Thread::raise(Args&&... args)
    {
      auto e = Exception{std::forward<Args>(args)...};
      this->raise(std::make_exception_ptr(std::move(e)));
    }

    template <typename Exception, typename... Args>
    void
    Thread::raise_and_wake(Args&&... args)
    {
      auto e = Exception{std::forward<Args>(args)...};
      this->raise_and_wake(std::make_exception_ptr(std::move(e)));
    }

    template <typename R>
    VThread<R>::VThread(Scheduler& scheduler,
                        const std::string& name,
                        Action action)
      : Thread(scheduler, name,
               [action = std::move(action), res = std::ref(_result)]
               { vthread_catcher<R>(action, res); })
      , _result()
    {}

    template <typename R>
    const R&
    VThread<R>::result() const
    {
      if (state() != Thread::State::done)
        throw elle::Exception
          ("tried to fetched the result of an unfinished thread");
      return _result;
    }

    inline
    void
    Thread::Terminator::operator ()(reactor::Thread* t)
    {
      try
      {
        bool disposed = t->_dispose;
        if (t)
          t->terminate_now();
        if (!disposed)
          this->std::default_delete<reactor::Thread>::operator ()(t);
      }
      catch (...)
      {
        ELLE_ABORT("Thread::unique_ptr release of %s threw: %s",
                   t, elle::exception_string());
      }
    }

    template <typename ... Args>
    Thread::unique_ptr::unique_ptr(Args&& ... args)
      : std::unique_ptr<reactor::Thread, Terminator>(std::forward<Args>(args)...)
    {
      if (*this && this->get()->_dispose)
        this->_slot = this->get()->destructed().connect(
          [this] {this->release(); });
    }
  }
}
