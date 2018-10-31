#pragma once

#include <elle/reactor/scheduler.hh>

namespace elle
{
  namespace reactor
  {
    /*-------------.
    | Construction |
    `-------------*/

    template <typename T, typename Container>
    Channel<T, Container>::Channel()
      : _read_barrier("channel read")
      , _write_barrier("channel write")
      , _opened(true)
      , _max_size(SizeUnlimited)
    {}

    template <typename T, typename Container>
    Channel<T, Container>::Channel(Self&& source)
      : _read_barrier(std::move(source._read_barrier))
      , _write_barrier(std::move(source._write_barrier))
      , _queue(std::move(source._queue))
      , _opened(source._opened)
      , _max_size(source._max_size)
    {}

    /*--------.
    | Content |
    `--------*/

    template <typename T, typename Container>
    bool
    Channel<T, Container>::empty() const
    {
      return this->_queue.empty();
    }

    template <typename T, typename Container>
    void
    Channel<T, Container>::put(T data)
    {
      ELLE_LOG_COMPONENT("elle.reactor.Channel");
      ELLE_TRACE_SCOPE("%s: put", this);
      if (signed(this->_queue.size()) >= this->_max_size)
      {
        ELLE_DEBUG("at capacity, wait");
        do
        {
          this->_write_barrier.close();
          reactor::wait(this->_write_barrier);
        }
        while (signed(this->_queue.size()) >= this->_max_size);
        ELLE_DEBUG("gained capacity, resume put");
      }
      this->_queue.push(std::move(data));
      if (this->_opened && !this->_read_barrier.opened())
      {
        ELLE_DEBUG("open");
        this->_read_barrier.open();
      }
      this->_on_put();
    }

    template <typename T, typename Container>
    template <typename... Args>
    void
    Channel<T, Container>::emplace(Args&&... args)
    {
      put(T(std::forward<Args>(args)...));
    }

    namespace details
    {
      template<typename T>
      typename T::value_type&
      queue_front(T& container)
      {
        return container.front();
      }

      template<typename T>
      typename std::priority_queue<T>::value_type&
      queue_front(std::priority_queue<T>& container)
      {
        // top() returns const& because modifying the object would break the
        // priority_queue invariants.
        // Let's hope that poping it rigth after is safe. It would make sense.
        return const_cast<typename std::priority_queue<T>::value_type&>
          (container.top());
      }
    }

    template <typename T, typename Container>
    T
    Channel<T, Container>::get()
    {
      ELLE_LOG_COMPONENT("elle.reactor.Channel");
      ELLE_TRACE_SCOPE("%s: get", this);
      if (!this->_read_barrier.opened())
      {
        ELLE_TRACE_SCOPE("wait for data");
        // Loop in case the channel was exhausted by another reader.
        while(!this->_read_barrier.opened())
          reactor::wait(this->_read_barrier);
      }
      else if (this->_queue.empty() && this->_exception)
        std::rethrow_exception(this->_exception);
      ELLE_ASSERT(!this->_queue.empty());
      T res(std::move(details::queue_front(this->_queue)));
      this->_queue.pop();
      ELLE_DEBUG("got data")
        ELLE_DUMP("value: %s", res);
      if (this->_queue.empty())
        this->_exhausted();
      if (signed(this->_queue.size()) < this->_max_size)
        this->_write_barrier.open();
      this->_on_get();
      return res;
    }

    template <typename T, typename Container>
    void
    Channel<T, Container>::_exhausted()
    {
      ELLE_LOG_COMPONENT("elle.reactor.Channel");
      ELLE_DEBUG_SCOPE("exhausted all data, close");
      ELLE_ASSERT(this->_queue.empty());
      this->_read_barrier.close();
      if (this->_exception)
      {
        ELLE_TRACE("raise %s", elle::exception_string(this->_exception));
        this->_read_barrier.raise(this->_exception);
      }
    }

    template <typename T, typename Container>
    void
    Channel<T, Container>::raise(std::exception_ptr e)
    {
      ELLE_LOG_COMPONENT("elle.reactor.Channel");
      this->_exception = e;
      if (this->_queue.empty())
        ELLE_TRACE("%s: raise %s", this, elle::exception_string(e))
          this->_read_barrier.raise(e);
      else
        ELLE_TRACE("%s: raise %s when empty", this, elle::exception_string(e));
    }

    template <typename T, typename Container>
    template <typename E, typename ... Args>
    void
    Channel<T, Container>::raise(Args&& ... args)
    {
      try
      {
        throw E(std::forward<Args>(args)...);
      }
      catch (...)
      {
        this->raise(std::current_exception());
      }
    }

    template <typename T, typename Container>
    int
    Channel<T, Container>::size() const
    {
      return this->_queue.size();
    }

    template <typename T, typename Container>
    void
    Channel<T, Container>::max_size(int ms)
    {
      this->_max_size = ms;
      if (this->_max_size < signed(this->_queue.size()))
        this->_write_barrier.open();
      // no need to close, next write will do that
    }

    template <typename T, typename Container>
    const T&
    Channel<T, Container>::peek() const
    {
      ELLE_LOG_COMPONENT("elle.reactor.Channel");
      ELLE_TRACE_SCOPE("%s: peek", this);
      while (!this->_read_barrier.opened())
      {
        ELLE_TRACE_SCOPE("wait for data");
        reactor::wait(elle::unconst(this->_read_barrier));
      }
      ELLE_ASSERT(!this->_queue.empty());
      return details::queue_front(this->_queue);
    }

    template <typename T, typename Container>
    void
    Channel<T, Container>::clear()
    {
      ELLE_LOG_COMPONENT("elle.reactor.Channel");
      ELLE_TRACE_SCOPE("%s: clear", *this);
      this->_queue = Container(); // priority_queue has no clear
      if (this->_max_size < signed(this->_queue.size()))
        this->_write_barrier.open();
      if (this->_read_barrier.opened())
        this->_exhausted();
    }

    /*--------.
    | Control |
    `--------*/

    template <typename T, typename Container>
    void
    Channel<T, Container>::open()
    {
      ELLE_LOG_COMPONENT("elle.reactor.Channel");
      ELLE_TRACE_SCOPE("%s: open", *this);
      if (!this->_opened)
      {
        this->_opened = true;
        if (!this->_queue.empty())
          this->_read_barrier.open();
      }
    }

    template <typename T, typename Container>
    void
    Channel<T, Container>::close()
    {
      ELLE_LOG_COMPONENT("elle.reactor.Channel");
      ELLE_TRACE_SCOPE("%s: close", *this);
      this->_opened = false;
    }

    /*----------.
    | Printable |
    `----------*/

    template <typename T, typename Container>
    void
    Channel<T, Container>::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "Channel(%x)", (void*)this);
    }
  }
}
