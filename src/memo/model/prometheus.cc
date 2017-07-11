#if MEMO_ENABLE_PROMETHEUS
# include <memo/model/prometheus.hh>

# include <boost/exception/diagnostic_information.hpp>

# include <elle/Error.hh>
# include <elle/log.hh>
# include <elle/os/environ.hh>
# include <elle/printf.hh>

# include <prometheus/exposer.h>
# include <prometheus/registry.h>

ELLE_LOG_COMPONENT("memo.prometheus");

using namespace std::literals;

namespace
{
  auto prometheus_endpoint
    = elle::os::getenv("MEMO_PROMETHEUS_ENDPOINT", "127.0.0.1:8080"s);
}

namespace memo
{
  namespace prometheus
  {
    void endpoint(std::string e)
    {
      ELLE_TRACE("setting endpoint to %s", e);
      ::prometheus_endpoint = std::move(e);
      instance().bind(::prometheus_endpoint);
    }

    std::string const& endpoint()
    {
      return ::prometheus_endpoint;
    }

    Prometheus& instance()
    {
      static auto res = Prometheus(endpoint());
      return res;
    }

    Prometheus::Prometheus(std::string const& addr)
    {
      bind(addr);
    }

    void
    Prometheus::bind(std::string const& addr)
    {
      if (addr != "no" && addr != "0")
        try
        {
          if (this->_exposer)
          {
            ELLE_LOG("%s: rebind on %s", this, addr);
            this->_exposer->rebind(addr);
          }
          else
          {
            ELLE_LOG("%s: listen on %s", this, addr);
            this->_exposer = std::make_unique<::prometheus::Exposer>(addr);
          }
        }
        catch (std::runtime_error const&)
        {
          ELLE_WARN("%s: creation failed, metrics will not be exposed: %s",
                    this, elle::exception_string());
          this->_exposer.reset();
        }
    }

    /// An HTTP server to answer Prometheus' requests.
    ///
    /// Maybe nullptr if set up failed.
    ::prometheus::Exposer*
    exposer()
    {
      return instance()._exposer.get();
    }

    /// Where to register the measurements to expose to Prometheus.
    std::shared_ptr<::prometheus::Registry>
    registry()
    {
      // Build and ask the exposer to scrape the registry on incoming
      // scrapes.
      static auto res = []() -> std::shared_ptr<::prometheus::Registry>
      {
        if (auto e = exposer())
        {
          auto res = std::make_shared<::prometheus::Registry>();
          e->RegisterCollectable(res);
          return res;
        }
        else
          return nullptr;
      }();
      return res;
    }

    auto
    Prometheus::make_gauge_family(std::string const& name,
                                  std::string const& help)
      -> Family<Gauge>*
    {
      // Add a new member gauge family to the registry.
      if (auto reg = registry())
      {
        ELLE_TRACE("creating gauge family %s", name);
        auto& res = ::prometheus::BuildGauge()
          .Name(name)
          .Help(help)
          .Register(*reg);
        return &res;
      }
      else
        return {};
    }

    auto
    Prometheus::make_counter_family(std::string const& name,
                                    std::string const& help)
      -> Family<Counter>*
    {
      // Add a counter and register it.
      if (auto reg = registry())
      {
        ELLE_TRACE("creating counter family %s", name);
        auto& res = ::prometheus::BuildCounter()
          .Name(name)
          .Help(help)
          .Register(*reg);
        return &res;
      }
      else
        return {};
    }

    auto
    Prometheus::make(Family<Counter>* family, Labels const& labels)
      -> UniquePtr<Counter>
    {
      if (family)
      {
        ELLE_TRACE("creating %s counter: %s", family->name(), labels);
        return {&family->Add(labels), Deleter<Counter>{family}};
      }
      else
        return {};
    }

    auto
    Prometheus::make(Family<Gauge>* family, Labels const& labels)
      -> UniquePtr<Gauge>
    {
      if (family)
      {
        ELLE_TRACE("creating %s gauge: %s", family->name(), labels);
        return {&family->Add(labels), Deleter<Gauge>{family}};
      }
      else
        return {};
    }
  }
}
#endif
