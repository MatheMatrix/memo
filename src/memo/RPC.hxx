namespace memo
{
  template <typename R, typename ... Args>
  R
  RPC<R (Args...)>::operator ()(Args const& ... args)
  {
    return RPCCall<R (Args...)>::_call(this->_version, *this, args...);
  }

  inline
  std::ostream&
  operator <<(std::ostream& o, BaseRPC const& rpc)
  {
    return elle::fprintf(o, "RPC(%s)", rpc.name());
  }
}
