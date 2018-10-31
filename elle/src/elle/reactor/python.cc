#include <boost/python.hpp>
// Retarded CPython defines these macros...
// http://bugs.python.org/issue10910
#ifdef tolower
# undef isalnum
# undef isalpha
# undef islower
# undef isspace
# undef isupper
# undef tolower
# undef toupper
#endif

#include <boost/function.hpp>

#include <elle/reactor/exception.hh>
#include <elle/reactor/python.hh>
#include <elle/reactor/scheduler.hh>

using boost::python::class_;

// Pacify -Wmissing-declarations
extern "C"
{
  PyObject* PyInit_reactor();
}

/*-----------.
| Exceptions |
`-----------*/

static
PyObject* terminate_exception = nullptr;

static
void
translator(elle::reactor::Terminate const& e)
{
  PyErr_SetString(terminate_exception, e.what());
}

/*-------.
| Thread |
`-------*/

// Shamelessly taken from the internet:
// http://misspent.wordpress.com/2009/10/11/boost-python-and-handling-python-exceptions/
static
void
wrap(elle::reactor::Thread::Action const& action)
{
  try
  {
    action();
  }
  catch (const boost::python::error_already_set&)
  {
    auto python_exception = std::current_exception();
    using namespace boost::python;

    PyObject *e, *v, *t;
    PyErr_Fetch(&e, &v, &t);

    // A NULL e means that there is not available Python exception
    if (!e)
      return;

    // See if the exception was Terminate. If so, throw a C++ version of that
    // exception
    if (PyErr_GivenExceptionMatches(e, terminate_exception))
    {
      // We construct objects now since we plan to keep
      // ownership of the references.
      object e_obj(handle<>(allow_null(e)));
      object v_obj(handle<>(allow_null(v)));
      object t_obj(handle<>(allow_null(t)));

      throw elle::reactor::Terminate("restored from python (XXX)");
    }
    // We didn't do anything with the Python exception, and we never took
    // ownership of the refs, so it's safe to simply pass them back to
    // PyErr_Restore
    PyErr_Restore(e, v, t);

    // Rethrow the exception.
    std::rethrow_exception(python_exception);
  }
}

namespace elle
{
  namespace reactor
  {
    PythonException::PythonException():
      elle::Exception("python error"), // XXX: pretty print the python value
      type(nullptr),
      value(nullptr),
      traceback(nullptr)
    {
      PyErr_Fetch(&this->type, &this->value, &this->traceback);
    }

    void
    PythonException::restore() const
    {
      PyErr_Restore(this->type, this->value, this->traceback);
      throw boost::python::error_already_set();
    }
  }
}

class Thread:
  public elle::reactor::Thread,
  public boost::python::wrapper<elle::reactor::Thread>
{
public:
  Thread(PyObject* instance,
         elle::reactor::Scheduler& s,
         std::string const& name,
         boost::function<void ()> const& action):
    elle::reactor::Thread(s, name, [action] { wrap(action); }, false),
    _self(instance)
  {
    boost::python::incref(instance);
  }
  Thread(PyObject* instance,
         std::string const& name,
         boost::function<void ()> const& action):
    elle::reactor::Thread(name, [action] { wrap(action); }, false),
    _self(instance)
  {
    boost::python::incref(instance);
  }
  ~Thread() override
  = default;

  void
  _scheduler_release() override
  {
    boost::python::decref(this->_self);
  }

protected:
  // FIXME: factor with parent method
  void
  _action_wrapper(const Thread::Action& action) override
  {
    ELLE_LOG_COMPONENT("elle.reactor.Thread");
    try
    {
      if (this->_exception)
      {
        ELLE_TRACE("%s: re-raise exception: %s",
                   *this, elle::exception_string(this->_exception));
        std::exception_ptr tmp = this->_exception;
        this->_exception = std::exception_ptr{};
        std::rethrow_exception(tmp);
      }
      action();
    }
    catch (elle::reactor::Terminate const&)
    {}
    catch (boost::python::error_already_set const&)
    {
      ELLE_WARN("%s: python exception escaped", *this);
      _exception_thrown = std::make_exception_ptr(
        elle::reactor::PythonException());
    }
    catch (elle::Exception const& e)
    {
      ELLE_WARN("%s: exception escaped: %s", *this, elle::exception_string())
      {
        ELLE_DUMP("exception type: %s", elle::demangle(typeid(e).name()));
        ELLE_DUMP("backtrace:\n%s", e.backtrace());
      }
      _exception_thrown = std::current_exception();
    }
    catch (...)
    {
      ELLE_WARN("%s: exception escaped: %s", *this, elle::exception_string());
      _exception_thrown = std::current_exception();
    }
  }

private:
  PyObject* _self;
};

// Shamelessly taken from the internet:
// http://stackoverflow.com/questions/9620268/boost-python-custom-exception-class
static
PyObject*
create_exception_class(const char* name,
                       PyObject* baseTypeObj = PyExc_Exception)
{
  namespace bp = boost::python;

  std::string scopeName =
    bp::extract<std::string>(bp::scope().attr("__name__"));
  std::string qualifiedName0 = scopeName + "." + name;
  auto* qualifiedName1 = const_cast<char*>(qualifiedName0.c_str());

  PyObject* typeObj = PyErr_NewException(qualifiedName1, baseTypeObj, nullptr);
  if(!typeObj) bp::throw_error_already_set();
  bp::scope().attr(name) = bp::handle<>(bp::borrowed(typeObj));
  return typeObj;
}

static void wait_wrap(elle::reactor::Thread* t)
{
  t->Waitable::wait();
}

class Scheduler:
  public elle::reactor::Scheduler,
  public boost::python::wrapper<elle::reactor::Scheduler>
{
public:
  using Super = elle::reactor::Scheduler;

private:
  void
  _rethrow_exception(std::exception_ptr e) const override
  {
    try
    {
      std::rethrow_exception(_eptr);
    }
    catch (elle::reactor::PythonException const& e)
    {
      e.restore();
    }
  }
};

BOOST_PYTHON_MODULE(reactor)
{
  terminate_exception = create_exception_class("Terminate");
  class_<Scheduler,
         boost::noncopyable>
    ("Scheduler", boost::python::init<>())
    .def("run", &elle::reactor::Scheduler::run)
    ;
  class_<elle::reactor::Thread,
         Thread,
         boost::noncopyable>
    ("Thread", boost::python::init<elle::reactor::Scheduler&,
                                   std::string const&,
                                   boost::python::object>())
    .def(boost::python::init<std::string const&,
                                   boost::python::object>())
    .def("wait", &wait_wrap)
    ;
  boost::python::def("yield_", elle::reactor::yield);
  boost::python::def(
    "sleep",
    static_cast<void (*)(elle::reactor::Duration)>(elle::reactor::sleep));
  boost::python::def(
    "scheduler", &elle::reactor::scheduler,
    boost::python::return_value_policy<
      boost::python::copy_non_const_reference
    >());

  boost::python::register_exception_translator<elle::reactor::Terminate>(
    translator);
}
