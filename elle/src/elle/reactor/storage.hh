#pragma once

#include <mutex>
#include <unordered_map>

#include <boost/optional.hpp>
#include <boost/signals2.hpp>

#include <elle/reactor/fwd.hh>

namespace elle
{
  namespace reactor
  {
    /// XXX[doc].
    template <typename T>
    class LocalStorage
    {
    public:
      using Self = LocalStorage<T>;
      LocalStorage();
      ~LocalStorage();
      operator T&();
      T&
      get(T const& def);
      T&
      get();

    private:
      template <typename Fun>
      T&
      _get(Fun fun);
      void
      _clean(Thread* t);
      using Content = std::unordered_map<void*, T>;
      Content _content;
      using Links = std::unordered_map<void*, boost::signals2::connection>;
      Links _links;
      std::mutex _mutex;
    };
  }
}

#include <elle/reactor/storage.hxx>
