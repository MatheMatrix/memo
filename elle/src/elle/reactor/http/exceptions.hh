#pragma once

#include <elle/Error.hh>
#include <elle/reactor/duration.hh>

namespace elle
{
  namespace reactor
  {
    namespace http
    {
      /// Fatal error on a request that was not successfully completed.
      class RequestError
        : public elle::Error
      {
      public:
        using Super = elle::Error;
        RequestError(std::string url, std::string error);
      private:
        ELLE_ATTRIBUTE_R(std::string, url);
        ELLE_ATTRIBUTE_R(std::string, error);
      };

      /// The server didn't send any response.
      class EmptyResponse
        : public RequestError
      {
      public:
        using Super = RequestError;
        EmptyResponse(std::string url);
      };

      /// The request timed out.
      class Timeout
        : public RequestError
      {
      public:
        using Super = RequestError;
        Timeout(std::string url,
                reactor::Duration const& timeout);
        ELLE_ATTRIBUTE_R(reactor::Duration, timeout);
      };

      /// DNS resolution failed.
      class ResolutionFailure
        : public RequestError
      {
      public:
        using Super = RequestError;
        ResolutionFailure(std::string url);
      };
    }
  }
}
