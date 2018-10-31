import collections
import itertools
import subprocess

import drake
import drake.cxx
import drake.git

def _default_make_binary():
  from drake.which import which
  to_try = [
    'make',
    'gmake',
    'mingw32-make',
    'mingw64-make',
  ]
  for binary in to_try:
    path = which(binary)
    if path is not None:
      return path

_DEFAULT_MAKE_BINARY = _default_make_binary()

class GNUBuilder(drake.Builder):

  def __init__(
      self,
      cxx_toolkit,
      targets = [],
      configure: """Configure script path (or None if no configure
      step is needed)""" = None,
      working_directory: "Deduced from configure" = None,
      configure_args: "Arguments of the configure script" = [],
      sources = [],
      make_binary: "Make binary" = _DEFAULT_MAKE_BINARY,
      makefile: "Makefile filename, used if not None" = None,
      build_args: "Additional arguments for the make command" = ['install'],
      additional_env: "Additional environment variables" = {},
      configure_interpreter = None,
      patch = None,
      configure_stdout: 'Show configure standard output' = False,
      build_stdout: 'Show build standard output' = False):
    self.__toolkit = cxx_toolkit
    self.__build_stdout = build_stdout
    self.__configure = configure
    self.__configure_args = configure_args
    self.__configure_interpreter = configure_interpreter
    self.__configure_stdout = configure_stdout
    self.__targets = list(targets)
    self.__make_binary = make_binary
    self.__makefile = makefile
    self.__build_args = build_args
    self.__env = {}
    self.__env.update(additional_env)
    self.__patch = patch
    if make_binary is not None:
        self.__env.setdefault('MAKE', make_binary.replace('\\', '/'))
    if working_directory is not None:
        self.__working_directory = working_directory
        if not self.__working_directory.exists():
          self.__working_directory.mkpath()
    else:
        if self.__configure is None:
            raise Exception(
                "Cannot deduce the working directory (no configure script)"
            )
        self.__working_directory = self.__configure.path().dirname()
    drake.Builder.__init__(
      self,
      (configure is not None and [configure] or []) + sources,
      self.__targets)
    if isinstance(cxx_toolkit.patchelf, drake.BaseNode):
      self.add_src(cxx_toolkit.patchelf)

  def execute(self):
    env = dict(self.__env)
    import os
    env.update(os.environ)
    with drake.CWDPrinter(drake.path_root() / drake.path_build() / self.work_directory):
      # Patch
      if self.__patch is not None:
        patch_path = str(drake.path_root() / self.__patch.path())
        patch_cmd = ['patch', '-N', '-p', '1', '-i', patch_path],
        if not self.cmd('Patch %s' % self.work_directory,
                        patch_cmd,
                        cwd = self.work_directory):
          return False
      # Configure step
      if self.__configure is not None:
         if not self.cmd('Configure %s' % self.work_directory,
                         self.command_configure,
                         cwd = self.work_directory,
                         env = env,
                         leave_stdout = self.__configure_stdout):
             return False
      # Build step
      if not self.cmd('Build %s' % self.work_directory,
                      self.command_build,
                      cwd = self.work_directory,
                      env = env,
                      leave_stdout = self.__build_stdout):
        return False
    for target in self.__targets:
      path = target.path().without_prefix(self.work_directory)
      if isinstance(target, drake.cxx.DynLib):
        rpath = '.'
      elif isinstance(target, drake.cxx.Executable):
        rpath = '../lib'
      else:
        continue
      with drake.WritePermissions(target):
        cmd = self.__toolkit.rpath_set_command(target.path(), rpath)
        if self.__toolkit.os is not drake.os.windows:
          if not self.cmd('Fix rpath for %s' % target.path(), cmd):
            return False
        if self.__toolkit.os is drake.os.macos:
          cmd = ['install_name_tool',
                 '-id', '@rpath/%s' % target.name().basename(),
                  str(target.path())]
          if not self.cmd('Fix rpath for %s' % target.path(), cmd):
            return False
          lib_dependecies = self.parse_otool_libraries(target.path())
          for dep in lib_dependecies:
            if dep.basename() in (t.path().basename() for t in self.__targets):
              cmd = [
                'install_name_tool',
                '-change',
                str(dep),
                '@rpath/%s' % dep.basename(),
                str(target.path()),
              ]
              if not self.cmd('Fix dependency name for %s' % target.path(), cmd):
                return False
    return True

  def parse_otool_libraries(self, path):
    command = ['otool', '-L', str(path)]
    return [drake.Path(line[1:].split(' ')[0])
            for line
            in subprocess.check_output(command).decode().split('\n')
            if line.startswith('\t')]

  @property
  def command_configure(self):
    if self.__configure is None:
        return None
    config = [str(drake.path_build(absolute = True) / self.__configure.path())]
    if self.__configure_interpreter is not None:
      config.insert(0, self.__configure_interpreter)
    return config + self.__configure_args

  @property
  def command_build(self):
    if self.__makefile is not None:
      return [self.__make_binary, '-f', self.__makefile, 'install'] + self.__build_args
    return [self.__make_binary] + self.__build_args

  @property
  def work_directory(self):
    return str(self.__working_directory)


  def hash(self):
    env = {}
    env.update(self.__env)
    env.pop('DRAKE_RAW', '1')
    return ''.join([
      str(self.command_configure),
      str(self.command_build),
      str(tuple(sorted(env))),
    ])

  def __str__(self):
    return '%s(%s)' % (self.__class__.__name__, self.__working_directory)

class FatLibraryGenerator(drake.Builder):

  def __init__(self,
               input_libs,
               output_lib,
               headers = [],
               input_headers = None,
               output_headers = None):
    drake.Builder.__init__(self,
                           input_libs,
                           itertools.chain([output_lib], (drake.node(output_headers / p)
                                                for p in headers)))
    self.__input_libs = input_libs
    self.__output_lib = output_lib
    self.__headers = headers
    if input_headers:
      self.__input_headers = drake.path_build(input_headers)
    else:
      self.__input_headers = None
    if output_headers:
      self.__output_headers = drake.path_build(output_headers)
    else:
      self.__output_headers = None

  def execute(self):
    res = self.cmd('Lipo %s' % self.input_paths,
                   self.lipo_command,
                   leave_stdout = False)
    if not res:
      return False
    if self.__headers and self.__input_headers and self.__output_headers:
      res = self.cmd('cp %s' % self.__input_headers,
                     self.copy_headers_command,
                     leave_stdout = False)
    return res

  @property
  def lipo_command(self):
    if len(self.__input_libs) == 1:
      res = ['cp']
      res.extend(self.input_paths)
      res.append(self.__output_lib.path())
    else:
      res = ['lipo']
      res.extend(self.input_paths)
      res.extend(['-create', '-output'])
      res.append(self.__output_lib.path())
    return res

  @property
  def input_paths(self):
    res = []
    for input in self.__input_libs:
      res.append(input.path())
    return res

  @property
  def copy_headers_command(self):
    return ['cp', '-r',
            self.__input_headers, self.__output_headers]


class VersionGenerator(drake.Builder):

  def __init__(self, output, git = None, production_build = True):
    git = git or drake.git.Git()
    drake.Builder.__init__(self, [git], [output])
    self.__git = git
    self.__output = output
    self.__production_build = production_build

  def execute(self):
    self.output('Generate %s' % self.__output.path())
    chunks = collections.OrderedDict()
    if self.__production_build:
      version = self.__git.description()
    else:
      version = '%s-dev' % self.__git.version().split('-')[0]
    chunks['version'] = version
    chunks['major'], chunks['minor'], chunks['subminor'] = \
      map(int, version.split('-')[0].split('.'))
    with open(str(self.__output.path()), 'w') as f:
      variables = (self._variable(*item) for item in chunks.items())
      for line in itertools.chain(
          self._prologue(), variables, self._epilogue()):
        print(line, file = f)
    return True

  def _prologue(self):
    return iter(())

  def _epilogue(self):
    return iter(())

  def _variable(self, name, value):
    raise NotImplementedError()

  def hash(self):
    return self.__production_build


class PythonVersionGenerator(VersionGenerator):

  def _variable(self, name, value):
    return '%s = %s' % (name, repr(value))

class CxxVersionGenerator(VersionGenerator):

  def __init__(self, prefix, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self.__prefix = prefix

  def _variable(self, name, value):
    try:
      return '#define %s_%s %s' % \
        (self.__prefix.upper(), name.upper(), int(value))
    except:
      return '#define %s_%s "%s"' % \
        (self.__prefix.upper(), name.upper(), value)

  def _prologue(self):
    yield '#ifndef %s_GIT_VERSION_HH' % self.__prefix
    yield '# define %s_GIT_VERSION_HH' % self.__prefix
    yield ''

  def _epilogue(self):
    yield ''
    yield '#endif'


def set_local_libcxx(cxx_toolkit):
  def _set_local_libcxx(tgt):
    if cxx_toolkit.os in [drake.os.macos]:
      with drake.WritePermissions(drake.node(tgt)):
        return drake.command([
          'install_name_tool', '-change',
          '/usr/lib/libc++.1.dylib', '@rpath/libc++.1.dylib', str(drake.path_build(tgt, True))
          ])
    else:
      return True
  return _set_local_libcxx

class Keychain():

  def __init__(self, keychain_path, keychain_password):
    self.__keychain = str(keychain_path)
    self.__keychain_password = keychain_password

  def _unlock_keychain(self):
    output = subprocess.check_output(
      ['security', 'list-keychains']).decode('utf-8').split('\n')
    found = False
    existing_keychains = []
    for keychain in output:
      # Don't want to re-add the system keychain.
      if len(keychain.strip()) > 0:
        if keychain.strip(' "') != '/Library/Keychains/System.keychain':
          existing_keychains.append(keychain.strip(' "'))
      if keychain.strip(' "') == self.__keychain:
        found = True
    if not found:
      args = ['security', 'list-keychains', '-s']
      args.extend(existing_keychains)
      args.append(self.__keychain)
      subprocess.check_call(args)
    subprocess.check_call(['security', 'unlock-keychain', '-p',
                           self.__keychain_password, self.__keychain])

  def _lock_keychain(self):
    subprocess.check_call(['security', 'lock-keychain', self.__keychain])

  def __enter__(self):
    self._unlock_keychain()

  def __exit__(self, *args):
    self._lock_keychain()
