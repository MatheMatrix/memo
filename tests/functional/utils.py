import cryptography

import json
import pipes
import shutil
import subprocess
import sys
import tempfile
import time
import os

import infinit.beyond
import infinit.beyond.bottle
import infinit.beyond.couchdb

class TemporaryDirectory:

  def __init__(self):
    self.__dir = None

  def __enter__(self):
    self.__dir = tempfile.mkdtemp()
    return self

  def __exit__(self, *args, **kwargs):
    shutil.rmtree(self.__dir)

  @property
  def dir(self):
    return self.__dir


class Infinit(TemporaryDirectory):

  def __init__(self, beyond = None):
    self.__beyond = beyond

  def run(self, args, input = None, return_code = 0, env = {}, input_as_it_is = False):
    self.env = {
      'PATH': 'bin:backend/bin:/bin:/usr/sbin',
      'INFINIT_HOME': self.dir,
      'INFINIT_RDV': ''
    }
    if self.__beyond is not None:
      self.env['INFINIT_BEYOND'] = self.__beyond.domain
    self.env.update(env)
    pretty = '%s %s' % (
      ' '.join('%s=%s' % (k, v) for k, v in self.env.items()),
      ' '.join(pipes.quote(arg) for arg in args))
    print(pretty)
    if input is not None and not input_as_it_is:
      if isinstance(input, list):
        input = '\n'.join(map(json.dumps, input)) + '\n'
      else:
        input = json.dumps(input) + '\n'
      input = input.encode('utf-8')
    process = subprocess.Popen(
      args + ['-s'],
      env = self.env,
      stdin =  subprocess.PIPE,
      stdout =  subprocess.PIPE,
      stderr =  subprocess.PIPE,
    )
    if input is not None:
      # FIXME: On OSX, if you spam stdin before the FDStream takes it
      # over, you get a broken pipe.
      time.sleep(0.5)
    out, err = process.communicate(input)
    process.wait()
    if process.returncode != return_code:
      reason = err.decode('utf-8')
      print(reason, file = sys.stderr)
      raise Exception('command failed with code %s: %s (reason: %s)' % \
                      (process.returncode, pretty, reason))
    out = out.decode('utf-8')
    try:
    if len(out.split('\n')) > 2:
      out = '[' + out.replace('\n', ',')[0:-1] + ']'
      return json.loads(out)
    except:
      return out

  def run_script(self, user = None, volume='volume', seq = None, **kvargs):
    cmd = ['infinit-volume', '--run', volume]
    if user is not None:
      cmd += ['--as', user]
    response = self.run(cmd, input = seq or kvargs)
    return response

def assertEq(a, b):
  if a != b:
    raise Exception('%r != %r' % (a, b))

import bottle

class Beyond():

  def __init__(self):
    super().__init__()
    self.__server = bottle.WSGIRefServer(port = 0)
    self.__app = None
    self.__beyond = None
    self.__couchdb = infinit.beyond.couchdb.CouchDB()
    self.__datastore = None

  def __enter__(self):
    couchdb = self.__couchdb.__enter__()
    self.__datastore = \
      infinit.beyond.couchdb.CouchDBDatastore(couchdb)
    def run():
      self.__beyond = infinit.beyond.Beyond(
        datastore = self.__datastore,
        dropbox_app_key = 'db_key',
        dropbox_app_secret = 'db_secret',
        google_app_key = 'google_key',
        google_app_secret = 'google_secret',
      )
      self.__app = infinit.beyond.bottle.Bottle(beyond = self.__beyond)
      try:
        bottle.run(app = self.__app,
                   quiet = True,
                   server = self.__server)
      except Exception as e:
        raise e

    import threading
    from functools import partial
    thread = threading.Thread(target = run)
    thread.daemon = True
    thread.start()
    while self.__server.port == 0 and thread.is_alive():
      import time
      time.sleep(.1)
    if not thread.is_alive():
      raise Exception("Server is already dead")
    return self

  @property
  def domain(self):
    return "http://localhost:%s" % self.__server.port

  def __exit__(self, *args, **kwargs):
    pass

class User():

  def __init__(self, name, infinit):
    self.name = name
    self.storage = '%s/%s-storage' % (name, name)
    self.network = '%s/%s-network' % (name, name)
    self.volume = '%s/%s-volume' % (name, name)
    self.mountpoint = '%s/mountpoint' % infinit.dir
    os.mkdir(self.mountpoint)

    self.infinit = infinit

  def run(self, cli, **kargs):
    print('run as %s:\t' % self.name, cli)
    self.infinit.run(cli.split(' '), env = { 'INFINIT_USER': self.name }, **kargs)

  def run_split(self, args, **kargs):
    self.infinit.run(args, env = { 'INFINIT_USER': self.name }, **kargs)

  def async(self, cli, **kargs):
    thread = threading.Thread(
      target = partial(self.run, cli = cli, **kargs))
    thread.daemon = True
    thread.start()
    return thread

  def fail(self, cli, **kargs):
    self.infinit.run(cli.split(' '), return_code = 1, **kargs)
