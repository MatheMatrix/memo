#include <memory>

#include <boost/bind.hpp>
#include <boost/config/warning_disable.hpp>
#include <boost/format.hpp>
#include <boost/phoenix.hpp>
#include <boost/spirit/include/qi.hpp>

#include <elle/Printable.hh>
#include <elle/TypeInfo.hh>
#include <elle/assert.hh>
#include <elle/err.hh>
#include <elle/log.hh>
#include <elle/finally.hh>
#include <elle/find.hh>
#include <elle/print.hh>
#include <elle/utils.hh>
#include <elle/xalloc.hh>

ELLE_LOG_COMPONENT("elle.print")

namespace elle
{
  namespace _details
  {
    void
    default_print(std::ostream& o, std::type_info const& info, void const* p)
    {
      static auto const parsed = boost::format("%f(%x)");
      auto format = parsed;
      format % elle::type_info(info);
      format % p;
      o << format;
    }

    void
    err_nonbool(std::type_info const& info)
    {
      elle::err("type is not a truth value: {}", elle::type_info(&info));
    }

    /*----.
    | AST |
    `----*/

    class Expression
      : public elle::Printable
    {
    public:
      /// Easy down cast.  Unsafe (i.e., unchecked downcast).
      template <typename T>
      T const& as() const
      {
        return static_cast<T const&>(*this);
      }
    };

    class Composite
      : public Expression
    {
    public:
      using Exps = std::vector<std::shared_ptr<Expression>>;
      Composite(Exps exps = {})
        : expressions(std::move(exps))
      {}

      void
      print(std::ostream& s) const override
      {
        bool first = true;
        s << "Composite(";
        for (auto const& e: this->expressions)
        {
          if (first)
            first = false;
          else
            s << ", ";
          s << *e;
        }
        s << ')';
      }

      Exps expressions;
    };

    class Branch
      : public Expression
    {
    public:
      Branch(std::shared_ptr<Expression> cond,
             std::shared_ptr<Expression> then)
        : cond(std::move(cond))
        , then(std::move(then))
      {}

      virtual
      void
      print(std::ostream& s) const override
      {
        s << "Branch(";
        this->cond->print(s);
        s << ", ";
        this->then->print(s);
        s << ")";
      }

      std::shared_ptr<Expression> cond;
      std::shared_ptr<Expression> then;
    };

    class Index
      : public Expression
    {
    public:
      Index(int n)
        : n(n)
      {}

      static
      std::shared_ptr<Index>
      make(int n)
      {
        return std::make_shared<Index>(n);
      }

      virtual
      void
      print(std::ostream& s) const override
      {
        s << "Index(" << n << ')';
      }

      int n;
    };

    class Next
      : public Expression
    {
    public:
      Next()
      {}

      static
      std::shared_ptr<Next>
      make()
      {
        return std::make_shared<Next>();
      }

      virtual
      void
      print(std::ostream& s) const override
      {
        s << "Next()";
      }
    };

    /// A formatting request a la printf: %s, %d, etc.
    ///
    /// Also supports %r, like in Python, which is for debugging.
    class Legacy
      : public Expression
    {
    public:
      Legacy(std::vector<char> const& flags,
             boost::optional<unsigned> width,
             char fmt)
        : width(width)
        , fmt(fmt)
      {
        for (auto c: flags)
          switch (c)
          {
          case '-':
            this->positioning = left;
            break;
          case '+':
            this->showpos = true;
            break;
          case ' ':
          case '0':
            this->padding = c;
            break;
          }
      }

      static
      std::shared_ptr<Legacy>
      make(std::vector<char> const& flags,
           boost::optional<unsigned> width,
           char fmt)
      {
        return std::make_shared<Legacy>(flags, width, fmt);
      }

      virtual
      void
      print(std::ostream& s) const override
      {
        s << "Legacy(" << this->fmt << ')';
      }

      /// Where the padding is applied.
      enum Positioning { left, internal, right };
      Positioning positioning = internal;
      /// The width.
      boost::optional<unsigned> width;
      /// The format request: 's' for %s, 'd' for %d, etc.
      char fmt;
      char padding = ' ';
      bool showpos = false;
    };

    class Name
      : public Expression
    {
    public:
      Name(std::string n)
        : n(std::move(n))
      {}

      static
      std::shared_ptr<Name>
      make(std::string& n)
      {
        return std::make_shared<Name>(n);
      }

      virtual
      void
      print(std::ostream& s) const override
      {
        s << "Name(" << n << ')';
      }

      std::string n;
    };

    class Literal
      : public Expression
    {
    public:
      Literal(std::string text)
        : text(std::move(text))
      {}

      static
      std::shared_ptr<Literal>
      make(std::string& text)
      {
        return std::make_shared<Literal>(text);
      }

      virtual
      void
      print(std::ostream& s) const override
      {
        s << "Literal(" << text << ')';
      }

      std::string text;
    };

    /*--------.
    | Helpers |
    `--------*/

    namespace
    {
    void
    push(std::shared_ptr<Composite>& res, std::shared_ptr<Expression> e)
    {
      if (!res)
        res = std::make_shared<Composite>();
      res->expressions.emplace_back(std::move(e));
    }

    std::shared_ptr<Expression>
    make_fmt(std::shared_ptr<Expression> fmt,
             boost::optional<std::shared_ptr<Composite>> then = boost::none)
    {
      if (then)
        return std::make_shared<Branch>(std::move(fmt), std::move(then.get()));
      else
        return fmt;
    }

    /// Generate our format parser.
    ///
    /// Should be called once.
    auto
    make_parser()
    {
      namespace phoenix = boost::phoenix;
      namespace qi = boost::spirit::qi;
      namespace ascii = boost::spirit::ascii;
      using qi::labels::_1;
      using qi::labels::_2;
      using qi::labels::_3;
      using Iterator = std::string::const_iterator;

      using Res = std::shared_ptr<Expression>;
      using Exp = qi::rule<Iterator, Res()>;
      using Chr = qi::rule<Iterator, char()>;
      using Str = qi::rule<Iterator, std::string()>;
      using Phr = qi::rule<Iterator, std::shared_ptr<Composite>()>;

      static Phr phrase;
      static Chr escape
        = (qi::char_('\\') >> qi::char_("\\{}%"))[qi::_val = _2];
      static Str literal
        = *(escape | (qi::char_  - '{' - '}' - '\\' - '%') );
      static Exp plain
        = literal[qi::_val = phoenix::bind(&Literal::make, _1)];
      static Str identifier
        = ascii::alpha >> *ascii::alnum;
      static Exp var
        = identifier [qi::_val = phoenix::bind(&Name::make, _1)]
        | qi::int_   [qi::_val = phoenix::bind(&Index::make, _1)]
        | qi::eps    [qi::_val = phoenix::bind(&Next::make)];
      static Exp fmt
        = (var >> -("?" >> phrase))
        [qi::_val = phoenix::bind(&make_fmt, _1, _2)];
      static Exp legacy
        = ("%"
           >> *qi::char_("-+# 0'") // flags
           >> -qi::uint_           // width
           >> qi::char_("cdefgioprsuxCEGSX%"))
        [qi::_val = phoenix::bind(&Legacy::make, _1, _2, _3)];
      phrase
        = plain[phoenix::bind(&push, qi::_val, _1)]
        >> *(("{" >> fmt[phoenix::bind(&push, qi::_val, _1)] >> "}"
              | legacy[phoenix::bind(&push, qi::_val, _1)])
             >> plain[phoenix::bind(&push, qi::_val, _1)]);
      return phrase;
    }

    std::shared_ptr<Expression>
    parse(std::string const& input)
    {
      static auto const phrase = make_parser();
      namespace qi = boost::spirit::qi;
      auto res = std::shared_ptr<Expression>{};
      auto first = input.begin();
      auto last = input.end();
      if (!qi::phrase_parse(first, last, phrase, qi::eoi, res) || first != last)
        elle::err("invalid format: %s", input);
      ELLE_ASSERT(res);
      return res;
    }
    }

    /*------.
    | Print |
    `------*/

    /// @param count   the number of used arguments.
    static
    void
    print(std::ostream& s,
          Expression const& ast,
          std::vector<Argument> const& args,
          int& count,
          bool p,
          NamedArguments const& named,
          bool& full_positional)
    {
      auto const nth = [&] (int n) -> Argument const& {
        if (n < signed(args.size()))
          return args[n];
        else
          elle::err(
            "too few arguments for format: %s, expected at least %s",
            args.size(), n + 1);
      };
      auto* id = &typeid(ast);
      if (id == &typeid(Composite))
        for (auto const& e: ast.as<Composite>().expressions)
          print(s, *e, args, count, p, named, full_positional);
      else if (id == &typeid(Literal))
      {
        if (p)
          s.write(ast.as<Literal>().text.c_str(),
                  ast.as<Literal>().text.size());
      }
      else if (id == &typeid(Next))
      {
        if (p)
          nth(count)(s);
        ++count;
      }
      else if (id == &typeid(Legacy))
      {
        auto& leg = ast.as<Legacy>();
        if (p)
        {
          // FIXME: we need a saver that also deals with `repr`.
          auto const saver = boost::io::ios_all_saver(s);
          auto const old_repr = repr(s);
          if (leg.positioning == Legacy::left)
            s << std::left;
          else if (leg.positioning == Legacy::internal)
            s << std::internal;
          else if (leg.positioning == Legacy::right)
            s << std::right;
          if (leg.showpos)
            s << std::showpos;
          if (leg.width)
            s << std::setw(*leg.width);
          s << std::setfill(leg.padding);
          switch (auto c = leg.fmt)
          {
            case 'd':
            case 'i':
            case 'u':
              s << std::dec;
              break;
            case 'e':
              s << std::scientific;
              break;
            case 'f':
              s << std::fixed;
              break;
            case 'o':
              s << std::oct;
              break;
            case 'p':
            case 'x':
              s << std::hex;
              break;
            case 'r':
              repr(s, true);
              break;
            case 's':
              break;
            case '%':
              s << '%';
              repr(s, old_repr);
              return;
            default:
              ELLE_WARN("unsupported legacy format: %s", c)
              // FIXME
              break;
          }
          nth(count)(s);
          repr(s, old_repr);
        }
        ++count;
      }
      else if (id == &typeid(Index))
      {
        full_positional = false;
        if (p)
          nth(ast.as<Index>().n)(s);
      }
      else if (id == &typeid(Name))
      {
        auto const& name = ast.as<Name>().n;
        auto const it = named.find(name);
        if (it == named.end())
          elle::err("missing named format argument: %s", name);
        else if (p)
          it->second(s);
      }
      else if (id == &typeid(Branch))
      {
        auto const& branch = ast.as<Branch>();
        auto const& cond = *branch.cond;
        auto const* cid = &typeid(cond);
        if (cid == &typeid(Next))
        {
          p = p && bool(nth(count));
          ++count;
        }
        else if (cid == &typeid(Index))
          p = p && bool(nth(cond.as<Index>().n));
        else if (cid == &typeid(Name))
        {
          auto const& name = cond.as<Name>().n;
          if (auto it = elle::find(named, name))
            p = p && bool(it->second);
          else
            elle::err("missing named format argument: %s", name);
        }
        else
          elle::err("unexpected condition: %s", elle::type_info(cond));
        print(s, *branch.then, args, count, p, named, full_positional);
      }
    }

    void
    print(std::ostream& s,
          std::string const& fmt,
          std::vector<Argument> const& args,
          NamedArguments const& named)
    {
      auto const ast = _details::parse(fmt);
      int count = 0;
      bool full_positional = true;
      _details::print(s, *ast, args, count, true, named, full_positional);
      if (full_positional && count < signed(args.size()))
        elle::err("too many arguments (%s > %s) for format: %s",
                  args.size(), count, fmt);
    }
  }


  namespace
  {
    auto const repr_slot = xalloc<bool>{};
  }

  bool
  repr(std::ostream const& o)
  {
    // FIXME: de-unconst.
    return repr_slot(elle::unconst(o));
  }

  /// Set whether a stream is set for debugging output.
  void
  repr(std::ostream& o, bool d)
  {
    repr_slot(o) = d;
  }
}
