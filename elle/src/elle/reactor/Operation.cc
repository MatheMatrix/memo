#include <boost/lexical_cast.hpp>

#include <elle/assert.hh>
#include <elle/log.hh>
#include <elle/reactor/exception.hh>
#include <elle/reactor/Operation.hh>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Thread.hh>

ELLE_LOG_COMPONENT("elle.reactor.Operation");

namespace elle
{
  namespace reactor
  {
    Operation::Operation()
      : Operation(*Scheduler::scheduler())
    {}

    Operation::Operation(Scheduler& sched)
      : Super()
      , _running(false)
      , _sched(sched)
    {}

    void
    Operation::start()
    {
      ELLE_TRACE_SCOPE("%s: start", this);
      this->_running = true;
      this->_start();
    }

    bool
    Operation::join(DurationOpt timeout)
    {
      ELLE_TRACE_SCOPE("%s: wait for completion", this);
      try
      {
        if (!reactor::wait(*this, timeout))
        {
          ELLE_TRACE("%s: timed out", *this);
          this->abort();
          return false;
        }
      }
      catch (...)
      {
        ELLE_TRACE("%s: aborted by exception: %s",
                   *this, elle::exception_string());
        auto e = std::current_exception();
        try
        {
          this->abort();
        }
        catch (...)
        {
          ELLE_ERR("%s: dropping abort exception: %s",
                   this, elle::exception_string());
          throw;
        }
        std::rethrow_exception(e);
      }
      ELLE_TRACE("%s: done", *this);
      return true;
    }

    bool
    Operation::run(DurationOpt timeout)
    {
      this->start();
      return this->join(timeout);
    }

    void
    Operation::abort()
    {
      if (this->running())
      {
        ELLE_TRACE("%s: abort", this)
          this->_abort();
      }
    }

    void
    Operation::done()
    {
      this->_running = false;
      this->_signal();
    }
  }
}
