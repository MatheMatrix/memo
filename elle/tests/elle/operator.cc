#include <utility>

#include <boost/operators.hpp>

#include <elle/assert.hh>
#include <elle/operator.hh>
#include <elle/Exception.hh>

namespace test
{
  class Operator
    : public boost::totally_ordered<Operator>
  {
  public:
    Operator(std::string const& string):
      _string(new std::string(string))
    {
    }
    ~Operator()
    {
      delete this->_string;
    }

    bool
    operator <(Operator const& other) const
    {
      return (*this->_string < *other._string);
    }
    bool
    operator ==(Operator const& other) const
    {
      return (*this->_string == *other._string);
    }

  private:
    std::string* _string;
  };
}

int main()
{
  test::Operator op1("suce");
  test::Operator op2("mon");
  test::Operator op3("cul");

  ELLE_ASSERT(op1 != op2);
  ELLE_ASSERT(op2 > op3);
  ELLE_ASSERT(op1 >= op2);

  return 0;
}
