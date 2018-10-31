#ifdef ELLE_WINDOWS
# include <shlwapi.h>
#else
# include <fnmatch.h>
#endif

#include <functional>
#include <iterator> // cbegin, etc.
#include <memory>
#include <regex>
#include <thread>

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/thread/tss.hpp>
#include <boost/tokenizer.hpp>

#include <elle/Exception.hh>
#include <elle/Plugin.hh>
#include <elle/assert.hh>
#include <elle/find.hh>
#include <elle/log/Logger.hh>
#include <elle/os/environ.hh>
#include <elle/printf.hh> // for elle/err.hh
#include <elle/system/getpid.hh>

namespace elle
{
  namespace log
  {
    /*------------.
    | Indentation |
    `------------*/

    Indentation::~Indentation() = default;

    class PlainIndentation
      : public Indentation
    {
    public:
      PlainIndentation()
        : _indentation()
      {}

      int&
      indentation() override
      {
        if (!this->_indentation.get())
          this->_indentation.reset(new int(1));
        return *this->_indentation;
      }

      void
      indent() override
      {
        this->indentation() += 1;
      }

      void
      unindent() override
      {
        ELLE_ASSERT_GTE(this->indentation(), 1);
        this->indentation() -= 1;
      }

      std::unique_ptr<Indentation>
      clone() override
      {
        return std::make_unique<PlainIndentation>();
      }

    private:
      boost::thread_specific_ptr<int> _indentation;
    };

    int&
    Logger::indentation()
    {
      return this->_indentation->indentation();
    }

    void
    Logger::indent()
    {
      this->_indentation->indent();
    }

    void
    Logger::unindent()
    {
      this->_indentation->unindent();
    }

    /*-------------.
    | Construction |
    `-------------*/

    namespace
    {
      Logger::Level
      parse_level(std::string const& level)
      {
        if (level == "NONE")       return Logger::Level::none;
        else if (level == "LOG")   return Logger::Level::log;
        else if (level == "TRACE") return Logger::Level::trace;
        else if (level == "DEBUG") return Logger::Level::debug;
        else if (level == "DUMP")  return Logger::Level::dump;
        else elle::err("invalid log level: %s", level);
      }
    }

    Logger::Logger(std::string const& log_level,
                   std::string const& envvar)
      : _indentation{std::make_unique<PlainIndentation>()}
      , _time_universal{os::getenv("ELLE_LOG_TIME_UNIVERSAL", false)}
      , _time_microsec{os::getenv("ELLE_LOG_TIME_MICROSEC", false)}
      , _component_max_size{0}
    {
      this->_setup_indentation();
      // FIXME: resets indentation
      this->_connection = elle::Plugin<Indenter>::hook_added().connect(
        [this] (Indenter&) { this->_setup_indentation(); }
      );
      this->log_level(os::getenv(envvar, log_level));
    }

    Logger::~Logger() = default;

    void
    Logger::_setup_indentation()
    {
      auto factory = std::function<auto () -> std::unique_ptr<Indentation>>{
        [] () -> std::unique_ptr<Indentation>
        {
          return std::make_unique<PlainIndentation>();
        }
      };
      for (auto const& indenter: elle::Plugin<Indenter>::plugins())
        factory = [&indenter, factory]
          {
            return indenter.second->indentation(factory);
          };
      this->_indentation = factory();
    }

    void
    Logger::_log_level(std::string const& levels)
    {}

    void
    Logger::log_level(std::string const& levels)
    {
      using tokenizer = boost::tokenizer<boost::char_separator<char>>;
      auto const sep = boost::char_separator<char>{","};
      for (auto const& level: tokenizer{levels, sep})
      {
        static auto const re =
          std::regex{" *"
                     "(?:(.*)  *)?"     // 1: context
                     "(?:"
                     "([^ :]*)"         // 2: component
                     " *: *"
                     ")?"
                     "([^ :]*)"         // 3: level
                     " *"};

        auto m = std::smatch{};
        if (std::regex_match(level, m, re))
          this->_component_patterns
            .emplace_back(m[1],
                          m[2].length() ? m[2].str() : "*",
                          parse_level(m[3]));
        else
          elle::err("invalid level specification: %s", level);
      }
      this->_log_level(levels);
    }

    /*----------.
    | Messaging |
    `----------*/

    namespace
    {
      auto
      make_tags()
      {
        auto res = Tags{};
        for (auto const& tag: elle::Plugin<Tag>::plugins())
        {
          auto const& content = tag.second->content();
          if (!content.empty())
            res.emplace_back(tag.second->name(), content);
        }
        return res;
      }
    }

    auto
    Logger::make_message(Level level,
                         Type type,
                         std::string const& component,
                         std::string const& msg,
                         std::string const& file,
                         unsigned int line,
                         std::string const& function)
      -> Message
    {
      auto const now = Clock::now();
      auto res = Message
        {
          level,
          type,
          component,
          msg,
          file,
          line,
          function,
          this->indentation() - 1, // FIXME: Why this convention?
          now,
          make_tags(),
        };
      if (0 <= res.indentation)
        return res;
      else
      {
        auto err = Message
          {
            level,
            type,
            component,
            elle::print("negative indentation level on log: %s", msg),
            file,
            line,
            function,
            0,
            now,
            make_tags(),
          };
        this->_message(err);
        std::abort();
      }
    }

    void
    Logger::message(Level level,
                    Type type,
                    std::string const& component,
                    std::string const& msg,
                    std::string const& file,
                    unsigned int line,
                    std::string const& fun)
    {
      this->message(make_message(level, type, component, msg,
                                 file, line, fun));
    }

    void
    Logger::message(Message const& msg)
    {
      std::lock_guard<std::recursive_mutex> lock(_mutex);
      if (this->component_is_active(msg.component, msg.level))
        this->_message(msg);
    }

    /*--------.
    | Enabled |
    `--------*/

    namespace
    {
      bool
      _fnmatch(std::string const& pattern, std::string const& s)
      {
#ifdef ELLE_WINDOWS
        return ::PathMatchSpec(s.c_str(), pattern.c_str()) == TRUE;
#else
        return fnmatch(pattern.c_str(), s.c_str(), 0) == 0;
#endif
      }
    }

    bool
    Logger::Filter::match(std::string const& s) const
    {
      return _fnmatch(pattern, s);
    }

    bool
    Logger::Filter::match(component_stack_t const& stack) const
    {
      // Either there is no request on the context, or some component
      // matches it.
      using boost::algorithm::any_of;
      return (context.empty()
              || (any_of(stack,
                         [this](const auto& comp)
                         {
                           return _fnmatch(context, comp);
                         })));
    }

    bool
    Logger::Filter::match(std::string const& s,
                          component_stack_t const& stack) const
    {
      return this->match(s) && this->match(stack);
    }

    bool
    Logger::component_is_active(std::string const& name, Level level)
    {
      return this->_component_is_active(name, level);
    }

    bool
    Logger::_component_is_active(std::string const& name,
                                 Level level)
    {
      std::lock_guard<std::recursive_mutex> lock(_mutex);
      auto res = level <= this->component_level(name);
      // Update the max width of displayed component names.
      if (res)
        this->_component_max_size =
          std::max(this->_component_max_size,
                   static_cast<unsigned int>(name.size()));
      return res;
    }

    Logger::Level
    Logger::component_level(std::string const& name)
    {
      std::lock_guard<std::recursive_mutex> lock(_mutex);
      auto res = Level::log;
      if (auto i = elle::find(this->_component_levels, name))
        res = i->second;
      else
        for (auto const& filter: this->_component_patterns)
          if (filter.match(name))
          {
            if (filter.match(this->_component_stack))
              res = filter.level;
            // If enabled unconditionally, cache it.
            if (filter.context.empty())
              // Several filters might apply (e.g.,
              // $ELLE_LOG_LEVEL="LOG,DUMP"), keep the last one.
              this->_component_levels[name] = res;
          }
      return res;
    }

    /*-------------.
    | Components.  |
    `-------------*/

    void
    Logger::component_push(std::string const& name)
    {
      std::lock_guard<std::recursive_mutex> lock(_mutex);
      this->_component_stack.emplace_back(name);
    }

    void
    Logger::component_pop()
    {
      std::lock_guard<std::recursive_mutex> lock(_mutex);
      // FIXME: make this an assert.
      if (!this->_component_stack.empty())
        this->_component_stack.pop_back();
    }


    /*------.
    | Level |
    `------*/

    std::ostream&
    operator << (std::ostream& os, Logger::Level l)
    {
      switch (l)
      {
      case Logger::Level::none:  return os << "none";
      case Logger::Level::log:   return os << "log";
      case Logger::Level::trace: return os << "trace";
      case Logger::Level::debug: return os << "debug";
      case Logger::Level::dump:  return os << "dump";
      }
      elle::unreachable();
    }

    /*-----.
    | Tags |
    `-----*/

#define ELLE_LOGGER_TAG(Name, Content)                          \
    class Name##Tag                                             \
      : public elle::log::Tag                                   \
    {                                                           \
    public:                                                     \
      std::string                                               \
      content() override                                        \
      {                                                         \
        return boost::lexical_cast<std::string>(Content);       \
      }                                                         \
                                                                \
      std::string                                               \
      name() override                                           \
      {                                                         \
        return #Name;                                           \
      }                                                         \
    };                                                          \
                                                                \
    elle::Plugin<Tag>::Register<Name##Tag> register_tag_##Name

    ELLE_LOGGER_TAG(PID, elle::system::getpid());
    ELLE_LOGGER_TAG(TID, std::this_thread::get_id());

#undef ELLE_LOGGER_TAG
  }
}
