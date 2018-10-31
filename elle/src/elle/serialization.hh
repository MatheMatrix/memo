#ifndef ELLE_SERIALIZATION_TAG_HH
# define ELLE_SERIALIZATION_TAG_HH

# include <elle/Version.hh>

namespace elle
{
  /// The serialization tag used by Elle.
  struct ELLE_API serialization_tag
  {
    static elle::Version version;
  };
}

#endif
