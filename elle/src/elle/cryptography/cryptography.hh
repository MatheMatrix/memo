#ifndef ELLE_CRYPTOGRAPHY_CRYPTOGRAPHY_HH
# define ELLE_CRYPTOGRAPHY_CRYPTOGRAPHY_HH

namespace elle
{
  /// This namespace contains everything necessary for performing
  /// cryptographic operations.
  namespace cryptography
  {
    /*----------.
    | Functions |
    `----------*/

    /// Makes sure the cryptographic module has been set up. This method
    /// can be called whenever one needs the cryptography to be set up.
    void
    require();
  }
}

#endif
