# Copyright (C) 2013-2016, Quentin "mefyl" Hocquet
#
# This software is provided "as is" without warranty of any kind,
# either expressed or implied, including but not limited to the
# implied warranties of fitness for a particular purpose.
#
# See the LICENSE file for more information.

import os
import sys
import threading
import drake
import drake.enumeration

class LogLevel(drake.enumeration.Enumerated,
               values = ['log', 'trace', 'debug', 'dump'],
               orderable = True):
  pass


class Noop:

  def __init__(self):
    pass

  def __enter__(self):
    pass

  def __exit__(self, type, value, traceback):
    pass


NOOP = Noop()


class NoopLogger:

  def log(self, component, level, message, *args):
    return NOOP



class LoggerType(type):

  def __call__(self, configuration_string = None, indentation = None):
    if configuration_string is None:
      return NoopLogger()
    return type.__call__(self,
                         configuration_string = configuration_string,
                         indentation = indentation)

class Logger(metaclass = LoggerType):

  class Indentation:

    def __init__(self):
      self.__indentation = 0

    def __enter__(self):
      self.__indentation += 1

    def __exit__(self, type, value, traceback):
      self.__indentation -= 1

    @property
    def indentation(self):
      return self.__indentation

  def __init__(self, configuration_string = None, indentation = None):
    self.__indentation = indentation or Logger.Indentation()
    def parse_log_level(string):
      string = string.lower()
      if string == 'log':
        return LogLevel.log
      elif string == 'trace':
        return LogLevel.trace
      elif string == 'debug':
        return LogLevel.debug
      elif string == 'dump':
        return LogLevel.dump
      else:
        raise Exception('invalid log level: %s' % string)
    self.__components = {}
    if configuration_string is not None:
      for component in configuration_string.split(','):
        colons = component.count(':')
        if colons == 0:
          level = parse_log_level(component)
          component = None
        elif colons == 1:
          component, level = component.split(':')
          level = parse_log_level(level)
        else:
          raise Exception('invalid log configuration: %s' % component)
        if component is not None:
          self.__components[component] = level

  def log(self, component, level, message, *args):
    if level <= self.__components.setdefault(component, LogLevel.log):
      print('%s%s' % ('  ' * self.__indentation.indentation,
                      message % args),
            file = sys.stderr)
      return self.__indentation
    else:
      return NOOP


# DEBUG_TRACE = 1
# DEBUG_TRACE_PLUS = 2
# DEBUG_DEPS = 2
# DEBUG_SCHED = 3

# _DEBUG = 0
# if 'DRAKE_DEBUG' in os.environ:
#   _DEBUG = int(os.environ['DRAKE_DEBUG'])
# _INDENT = 0
# _DEBUG_SEM = threading.Semaphore(1)

# def debug(msg, lvl = 1):
#   if lvl <= _DEBUG:
#     with _DEBUG_SEM:
#       print('%s%s' % (' ' * _INDENT * 2, msg), file = sys.stderr)


# class indentation:

#   def __enter__(self):
#     global _INDENT
#     _INDENT += 1

#   def __exit__(self, type, value, traceback):
#     global _INDENT
#     _INDENT -= 1
