import base64
import os
import requests
import shutil
import subprocess
import sys

import infinit.beyond.version

from infinit.beyond import validation, emailer

from copy import deepcopy
from itertools import chain

exe_ext = os.environ.get('EXE_EXT', '')
host_os = os.environ.get('OS', '')
tmp_dir = os.environ.get('TMPDIR', '/tmp')

def log(fmt, *args):
  print('beyond:', fmt.format(*args), file=sys.stderr)

def run(*args, **kwargs):
  kwargs.setdefault('stdout', subprocess.PIPE)
  kwargs.setdefault('stderr', subprocess.PIPE)
  kwargs['universal_newlines'] = True
  log('run: {}'.format(' '.join(*args)))
  res = subprocess.run(*args, **kwargs)
  log('ran: {}'.format(res))
  return res

## ------------ ##
## Crash report ##
## ------------ ##

# Where the git repo for debug symbols is checkout.
repo_dir = os.path.join(tmp_dir, 'debug-symbols')

# Where the symbol files are.
symbols_dir = repo_dir + '/symbols'

def symbols_checkout():
  '''Fetch the debug-symbols git repository and checkout origin/master.
  Return whether success.'''
  # The repo is there.  Update it.
  p = run(['git', '-C', repo_dir, 'fetch'])
  if p.returncode:
    log('symbolize: symbols_checkout: cannot fetch git repo: {}', p.stderr)
    return False
  p = run(['git', '-C', repo_dir, 'checkout', '--force', 'origin/master'])
  if p.returncode:
    log('symbolize: symbols_checkout: cannot checkout git repo: {}', p.stderr)
    return False
  return True

def symbols_update():
  '''Create or update the symbols/ directory.'''
  # Try to update the checkout.
  if symbols_checkout():
    return True
  # We failed.  Maybe the repository is broken, erase it and restart.
  try:
    shutil.rmtree(repo_dir)
  except Exception as e:
    log('symbolize: symbols_update: cannot remove {}: {}',
        repo_dir, e)
    return False
  p = run(['git', 'clone',
           'git@git.infinit.sh:infinit/debug-symbols.git',
           repo_dir])
  if p.returncode:
    log('symbolize: symbols_update: cannot clone git repo: {}', p.stderr)
    return False
  # The repo is there.  It should be on origin/master, but let's play
  # it extra safe.
  return symbols_checkout()

def symbolize_dump(in_, out = None):
  '''Read this minidump file and save its content,
  symbolized if possible.  It is safe to use in_ == out.'''
  if not out:
    out = in_
  try:
    has_symbols = symbols_update()
    with open(out + '.tmp', 'wb') as o:
      # minidump_stackwalk fails if we pass a directory that does not
      # exist.
      p = run(['minidump_stackwalk', in_]
              + [symbols_dir] if has_symbols else [],
              stdout=o)
      if p.returncode:
        log("symbolize: error: {}", p.stderr)
      else:
        log("symbolize: success")
        os.rename(out + '.tmp', out)
        return
  except Exception as e:
    log("symbolize: fatal: {}", e)

  # Worst case: return the input file.
  try:
    os.rename(in_, out)
  except Exception as e:
    log("symbolize: cannot rename dump file: {}", e)


## -------- ##
## Binaries ##
## -------- ##

# Don't generate crash reports on our Beyond server.
os.environ['MEMO_CRASH_REPORT'] = '0'

def find_binaries():
  for path in chain(
      [os.environ.get('MEMO_BINARIES')],
      os.environ.get('PATH', '').split(':'),
      ['bin', '/opt/memo/bin'],
  ):
    if not path:
      continue
    if not path.endswith('/'):
      path += '/'
    try:
      args = [path + 'memo' + exe_ext, '--version']
      subprocess.check_call(args)
      log('find_binaries: {} works', args)
      return path
    except FileNotFoundError:
      log('find_binaries: {} does not exist', args)
    except subprocess.CalledProcessError as e:
      log('find_binaries: {} failed: {}', args, e)
  log('find_binaries: could not find `memo`')
  return None

# Our bindir, with a trailing slash, or None if we can't find `memo`.
binary_path = find_binaries()

# Email templates.
templates = {
  'Drive/Joined': {
    'template': 'tem_RFSDrp7nzCbsBRSUts7MsU',
  },
  'Drive/Invitation': {
    'template': 'tem_UwwStKnWCWNU5VP4HBS7Xj',
  },
  'Drive/Plain Invitation': {
    'template': 'tem_j8r5aDLJ6v3CTveMahtauX',
  },
  'Internal/Crash Report': {
    'template': 'tem_fu5GEE6jxByj2SB4zM6CrH',
  },
  'Internal/Passport Generation Error': {
    'template': 'tem_LdEi9v8WrTACa8BNUhoSte',
  },
  'User/Welcome': {
    'template': 'tem_Jsd948JkLqhBQs3fgGZSsS',
  },
  'User/Confirmation Email': {
    'template': 'tem_b6ZtsWVHKzv4PUBDU7WTZj',
  },
  'Sales/New Customer': {
    'template': 'tem_SEWGs7yLLupWz9nXCmGq9J'
  }
}
# Make sure templates only contains entires named 'template' and 'version'.
import itertools
assert set(itertools.chain(*[list(x.keys()) for x in templates.values()])) == \
       {'template'}

class Beyond:

  def __init__(
      self,
      datastore,
      dropbox_app_key,
      dropbox_app_secret,
      google_app_key,
      google_app_secret,
      gcs_app_key,
      gcs_app_secret,
      sendwithus_api_key = None,
      limits = {},
      delegate_user = 'hub',
      keep_deleted_users = False,
  ):
    self.__datastore = datastore
    self.__datastore.beyond = self
    self.__dropbox_app_key    = dropbox_app_key
    self.__dropbox_app_secret = dropbox_app_secret
    self.__google_app_key    = google_app_key
    self.__google_app_secret = google_app_secret
    self.__gcs_app_key    = gcs_app_key
    self.__gcs_app_secret = gcs_app_secret
    self.__limits = limits
    if sendwithus_api_key is not None:
      self.__emailer = emailer.SendWithUs(sendwithus_api_key)
    else:
      self.__emailer = emailer.NoOp()
    self.__delegate_user = delegate_user
    self.__keep_deleted_users = keep_deleted_users

  @property
  def limits(self):
    return self.__limits

  @property
  def now(self):
    return self.__now()

  def __now(self):
    import datetime
    return datetime.datetime.utcnow()

  @property
  def dropbox_app_key(self):
    return self.__dropbox_app_key

  @property
  def emailer(self):
    return self.__emailer

  def template(self, name):
    if isinstance(self.__emailer, emailer.SendWithUs):
      return templates[name]
    else:
      return {
        'template': name,
        'version': None,
      }

  @property
  def dropbox_app_secret(self):
    return self.__dropbox_app_secret

  @property
  def google_app_key(self):
    return self.__google_app_key

  @property
  def google_app_secret(self):
    return self.__google_app_secret

  @property
  def gcs_app_key(self):
    return self.__gcs_app_key

  @property
  def gcs_app_secret(self):
    return self.__gcs_app_secret

  def is_email(self, email):
    try:
      validation.Email('user', 'email')(email)
      return True
    except exceptions.InvalidFormat as e:
      return False

  @property
  def delegate_user(self):
    return self.__delegate_user

  ## ------- ##
  ## Pairing ##
  ## ------- ##

  def pairing_information_get(self, owner, passphrase_hash):
    json = self.__datastore.pairing_fetch(owner)
    pairing = PairingInformation.from_json(self, json)
    if passphrase_hash != pairing.passphrase_hash:
      raise ValueError('passphrase_hash')
    self.pairing_information_delete(owner)
    if self.now > pairing.expiration:
      raise exceptions.NoLongerAvailable(
        '%s pairing information' % owner)
    return pairing

  def pairing_information_delete(self, owner):
    self.__datastore.pairing_delete(owner)

  def pairing_information_status(self, owner):
    json = self.__datastore.pairing_fetch(owner)
    pairing = PairingInformation.from_json(self, json)
    if self.now > pairing.expiration:
      raise exceptions.NoLongerAvailable(
        '%s pairing information' % owner)
    return True

  ## ------- ##
  ## Network ##
  ## ------- ##

  def network_get(self, owner, name):
    return Network.from_json(
      self,
      self.__datastore.network_fetch(owner = owner, name = name))

  def network_delete(self, owner, name):
    return self.__datastore.network_delete(owner = owner, name = name)

  def network_volumes_get(self, network):
    return (
      Volume.from_json(self, json) for json in
      self.__datastore.networks_volumes_fetch(networks = [network]))

  def network_drives_get(self, network):
    return self.__datastore.network_drives_fetch(name = network.name)

  def network_key_value_stores_get(self, network):
    return (
      KeyValueStore.from_json(self, json) for json in
      self.__datastore.networks_key_value_stores_fetch(networks = [network]))

  def network_stats_get(self, name):
    return self.__datastore.network_stats_fetch(network = name)

  def network_purge(self, user, network):
    # Remove drives in case the user has already remove their volumes.
    # Only remove objects owned by the user.
    drives = self.network_drives_get(network = network)
    for d in drives:
      if d.owner_name == user.name:
        self.drive_delete(owner = d.owner_name, name = d.unqualified_name)
    volumes = self.network_volumes_get(network = network)
    for v in volumes:
      if v.owner_name == user.name:
        self.volume_delete(owner = v.owner_name, name = v.unqualified_name)
    key_value_stores = self.network_key_value_stores_get(network = network)
    for k in key_value_stores:
      if k.owner_name == user.name:
        self.key_value_store_delete(owner = k.owner_name,
                                    name = k.unqualified_name)

  ## ---- ##
  ## User ##
  ## ---- ##

  def users_get(self):
    return (User.from_json(self, user)
            for user in self.__datastore.users_fetch())

  def user_get(self, name):
    json = self.__datastore.user_fetch(name = name)
    return User.from_json(self, json)

  def user_by_short_key_hash(self, hash):
    json = self.__datastore.user_by_short_key_hash(hash = hash)
    return User.from_json(self, json)

  def users_by_email(self, email):
    users = self.__datastore.users_by_email(email = email)
    if len(users) == 0:
      raise User.NotFound()
    return [User.from_json(self, u) for u in users]

  def user_by_ldap_dn(self, dn):
    user = self.__datastore.user_by_ldap_dn(dn)
    return User.from_json(self, user)

  def user_deleted_get(self, name):
    return self.__datastore.user_deleted_get(name)

  def user_delete(self, name):
    if self.__keep_deleted_users:
      self.__datastore.user_deleted_add(name)
    return self.__datastore.user_delete(name = name)

  def user_networks_get(self, user):
    return (Network.from_json(self, json) for json in
            self.__datastore.user_networks_fetch(user = user))

  class UniqueEntity:

    def __init__(self):
      self.found = set()

    def __call__(self, x):
      if x['name'] in self.found:
        return True
      self.found.add(x['name'])
      return False

  def user_volumes_get(self, user):
    networks = (Network.from_json(self, json) for json in
                self.__datastore.user_networks_fetch(user = user))

    import itertools
    return (Volume.from_json(self, json) for json in
            itertools.filterfalse(
              Beyond.UniqueEntity(),
              itertools.chain(
                self.__datastore.networks_volumes_fetch(networks = networks),
                self.__datastore.user_volumes_fetch(user))))

  def user_drives_get(self, name):
    return self.__datastore.user_drives_fetch(name = name)

  def user_key_value_stores_get(self, user):
    networks = (Network.from_json(self, json) for json in
                self.__datastore.user_networks_fetch(user = user))

    import itertools
    return (KeyValueStore.from_json(self, json) for json in
            itertools.filterfalse(
              Beyond.UniqueEntity(),
              itertools.chain(
                self.__datastore.networks_key_value_stores_fetch(networks),
                self.__datastore.user_key_value_stores_fetch(user))))

  def user_purge(self, user):
    # Remove elements individually in case the user has already removed some.
    # Only remove objects owned by the user.
    drives = self.user_drives_get(name = user.name)
    for d in drives:
      if d.owner_name == user.name:
        self.drive_delete(owner = d.owner_name, name = d.unqualified_name)
    volumes = self.user_volumes_get(user = user)
    for v in volumes:
      if v.owner_name == user.name:
        self.volume_delete(owner = v.owner_name, name = v.unqualified_name)
    key_value_stores = self.user_key_value_stores_get(user = user)
    for k in key_value_stores:
      if k.owner_name == user.name:
        self.key_value_store_delete(owner = k.owner_name,
                                    name = k.unqualified_name)
    networks = self.user_networks_get(user = user)
    for n in networks:
      if n.owner_name == user.name:
        self.network_purge(user = user, network = n)
        self.network_delete(owner = n.owner_name, name = n.unqualified_name)

  ## ------ ##
  ## Volume ##
  ## ------ ##

  def volume_get(self, owner, name):
    return Volume.from_json(
      self,
      self.__datastore.volume_fetch(owner = owner, name = name))

  def volume_delete(self, owner, name):
    return self.__datastore.volume_delete(
      owner = owner, name = name)

  def volume_drives_get(self, name):
    return self.__datastore.volume_drives_fetch(name = name)

  def volume_purge(self, user, volume):
    # Only remove objects owned by the user.
    drives = self.volume_drives_get(name = volume.name)
    for d in drives:
      if d.owner_name == user.name:
        self.drive_delete(owner = d.owner_name, name = d.unqualified_name)

  ## ----- ##
  ## Drive ##
  ## ----- ##

  def drive_get(self, owner, name):
    return self.__datastore.drive_fetch(
      owner = owner, name = name)

  def drive_delete(self, owner, name):
    return self.__datastore.drive_delete(
        owner = owner, name = name)

  def process_invitations(self, user, email, drives):
    if binary_path is None:
      raise NotImplementedError()
    # The infinit executable (possibly memo.exe).
    memo_path = binary_path + 'memo' + exe_ext
    errors = []
    try:
      try:
        beyond = self.user_get(self.delegate_user)
      except User.NotFound:
        raise Exception('Unknown user \'%s\'' % self.delegate_user)
      import tempfile
      with tempfile.TemporaryDirectory() as directory:
        env = {
          'MEMO_DATA_HOME': str(directory),
          'MEMO_USER': self.delegate_user,
        }
        import os
        import json
        def import_data(type, data):
          args = [memo_path, type, 'import', '-s']
          try:
            subprocess.check_output(
              args,
              env = env,
              input = (json.dumps(data) + '\n').encode('utf-8'),
              timeout = 5 if host_os != 'windows' else 15)
          except Exception as e:
            raise Exception('impossible to import %s \'%s\': %s' % (
                            type, data['name'], e))
        import_data('user', user.json())
        import_data('user', beyond.json(private = True))
        for drive in drives:
          try:
            try:
              network = self.network_get(*drive.network.split('/'))
            except Network.NotFound:
              raise Exception('Unknown network \'%s\'' % drive.network)
            import_data('network', network.json())
            output = subprocess.check_output(
              [
                memo_path, 'passport', 'create',
                '--user', user.name,
                '--network', network.name,
                '--as', self.delegate_user,
                '--output', '-',
                '--script',
              ],
              env = env)
            import json
            passport = json.loads(output.decode('ascii'))
            network.passports[user.name] = passport
            network.save()
            drive.users[user.name] = drive.users[email]
            drive.users[email] = None
            drive.save()
          except BaseException as e:
            errors.append(str(e))
    except BaseException as e:
      errors.append(str(e))
    if errors:
      self.__emailer.send_one(
        recipient_email = 'crash+passport_generation@infinit.sh',
        recipient_name = 'Crash',
        variables = {
          'user': user.name,
          'email': email,
          'errors': ' | '.join(errors),
        },
        **self.template('Internal/Passport Generation Error')
      )
    return errors

  ## --------------- ##
  ## Key Value Store ##
  ## --------------- ##

  def key_value_store_get(self, owner, name):
    return KeyValueStore.from_json(
      self,
      self.__datastore.key_value_store_fetch(owner = owner, name = name))

  def key_value_store_delete(self, owner, name):
    return self.__datastore.key_value_store_delete(owner = owner, name = name)

  ## ------------ ##
  ## Crash Report ##
  ## ------------ ##

  def crash_report_send(self, json):
    import tempfile
    from base64 import b64decode
    variables = {
      'platform': json.get('platform', 'Unknown'),
      'version': json.get('version', 'Unknown'),
    }
    with tempfile.TemporaryDirectory() as temp_dir:
      # A list of files (not file names), as requested by sendwithus.
      files = []
      for k, v in json.items():
        if k not in ['platform', 'version']:
          fname = '{}/client.{}'.format(temp_dir, k)
          with open(fname, 'wb') as f:
            f.write(b64decode(v))
          if k in ['dump']:
            symbolize_dump(fname)
          # 'rb' is requested by sendwithus.
          files.append(open(fname, 'rb'))
      self.__emailer.send_one(
        recipient_email = 'crash@infinit.sh',
        recipient_name = 'Crash',
        variables = variables,
        files = files,
        **self.template('Internal/Crash Report'))

class User:
  fields = {
    'mandatory': [
      ('name', validation.Name('user', 'name')),
      ('public_key', None),
    ],
    'optional': [
      ('description', validation.Description('user', 'description')),
      ('dropbox_accounts', None),
      ('email', validation.Email('user', 'email')),
      ('fullname', None),
      ('google_accounts', None),
      ('gcs_accounts', None),
      ('password_hash', None),
      ('private_key', None),
      ('ldap_dn', None),
    ]
  }
  class Duplicate(Exception):
    pass

  class NotFound(Exception):
    pass

  def __init__(self,
               beyond,
               name = None,
               email = None,
               fullname = None,
               public_key = None,
               password_hash = None,
               private_key = None,
               dropbox_accounts = None,
               google_accounts = None,
               gcs_accounts = None,
               emails = {},
               ldap_dn = None,
               description = None,
  ):
    self.__beyond = beyond
    self.__id = id
    self.__name = name
    self.__email = email
    self.__fullname = fullname
    self.__public_key = public_key
    self.__password_hash = password_hash
    self.__private_key = private_key
    self.__dropbox_accounts = dropbox_accounts or {}
    self.__dropbox_accounts_original = deepcopy(self.dropbox_accounts)
    self.__google_accounts = google_accounts or {}
    self.__google_accounts_original = deepcopy(self.google_accounts)
    self.__gcs_accounts = gcs_accounts or {}
    self.__gcs_accounts_original = deepcopy(self.gcs_accounts)
    self.__emails = emails
    if self.__email:
      if self.__email not in self.__emails:
        self.__emails[self.__email] = True
    self.__emails_original = deepcopy(self.emails)
    self.__ldap_dn = ldap_dn
    self.__description = description

  @classmethod
  def from_json(self, beyond, json, check_integrity = False):
    if check_integrity:
      for (key, validator) in User.fields['mandatory']:
        if key not in json:
          raise exceptions.MissingField('user', key)
        validator and validator(json[key])
      for (key, validator) in User.fields['optional']:
        if key in json and validator is not None:
          validator(json[key])
    return User(
      beyond,
      name = json['name'],
      public_key = json['public_key'],
      email = json.get('email', None),
      fullname = json.get('fullname', None),
      password_hash = json.get('password_hash', None),
      private_key = json.get('private_key', None),
      dropbox_accounts = json.get('dropbox_accounts', []),
      google_accounts = json.get('google_accounts', []),
      gcs_accounts = json.get('gcs_accounts', []),
      emails = json.get('emails', {}),
      ldap_dn = json.get('ldap_dn', None),
      description = json.get('description', None),
    )

  def json(self,
           private = False,
           hide_confirmation_codes = True):
    res = {
      'name': self.name,
      'public_key': self.public_key,
      'description': self.description,
    }
    if private:
      # Turn confirmations code into 'False'.
      def filter_confirmation_codes(key):
        if hide_confirmation_codes:
          if self.emails[key] != True:
            return (key, False)
        return (key, self.emails[key])
      res['emails'] = dict(map(filter_confirmation_codes,
                               self.emails))
      if self.email is not None:
        res['email'] = self.email
      if self.fullname is not None:
        res['fullname'] = self.fullname
      if self.dropbox_accounts is not None:
        res['dropbox_accounts'] = self.dropbox_accounts
      if self.google_accounts is not None:
        res['google_accounts'] = self.google_accounts
      if self.gcs_accounts is not None:
        res['gcs_accounts'] = self.gcs_accounts
      if self.private_key is not None:
        res['private_key'] = self.private_key
      if self.password_hash is not None:
        res['password_hash'] = self.password_hash
      if self.ldap_dn is not None:
        res['ldap_dn'] = self.ldap_dn
    return res

  def create(self):
    from uuid import uuid4
    if self.email:
      self.__emails[self.email] = str(uuid4())
    self.__beyond._Beyond__datastore.user_insert(self)
    if self.email is not None:
      self.__beyond.emailer.send_one(
        recipient_email = self.email,
        recipient_name = self.name,
        variables = {
          'email': self.email,
          'name': self.name,
          'url_parameters': self.url_parameters(self.email)
        },
        **self.__beyond.template('User/Welcome')
      )
      self.__beyond.emailer.send_one(
        recipient_email = 'sales@infinit.sh',
        recipient_name = 'sales',
        variables = {
          'user': {
            'email': self.email,
            'name': self.name,
          }
        },
        **self.__beyond.template('Sales/New Customer')
      )

  def confirmation_code(self, email):
    return self.__emails.get(email, None)

  def url_parameters(self, email):
    assert self.confirmation_code(email) is not None
    from urllib.parse import urlencode
    return urlencode({
      'name': self.name,
      'confirmation_code': self.confirmation_code(email),
      'email': email
    })

  def send_confirmation_email(self, email = None):
    email = email or self.email
    if email is not None:
      self.__beyond.emailer.send_one(
        recipient_email = email,
        recipient_name = self.name,
        variables = {
          'email': email,
          'name': self.name,
          'url_parameters': self.url_parameters(email)
        },
        **self.__beyond.template('User/Confirmation Email')
      )

  def save(self):
    diff = {}
    for type in ['dropbox', 'google', 'gcs']:
      original = getattr(self, '_User__%s_accounts_original' % type)
      accounts = getattr(self, '%s_accounts' % type)
      if accounts == {}:
        diff.setdefault('%s_accounts' % type, {}).update({
          k: None for k in original.keys()
        })
      else:
        for id, account in accounts.items():
          if original.get(id) != account:
            diff.setdefault('%s_accounts' % type, {})[id] = account
    for email, confirmation in self.emails.items():
      if self.__emails_original.get(email) != confirmation:
        diff.setdefault('emails', {})[email] = confirmation
    self.__beyond._Beyond__datastore.user_update(self.name, diff)
    self.__dropbox_accounts_original = dict(self.__dropbox_accounts)
    self.__google_accounts_original = dict(self.__google_accounts)
    self.__gcs_accounts_original = dict(self.__gcs_accounts)
    self.__emails_original = dict(self.__emails)

  @property
  def id(self):
    import hashlib
    der = base64.b64decode(self.public_key['rsa']) # .encode('latin-1'))
    sha = hashlib.sha256(der).digest()
    id = base64.urlsafe_b64encode(sha)[0:8]
    return id.decode('latin-1')

  @property
  def name(self):
    return self.__name

  @property
  def email(self):
    return self.__email

  @property
  def emails(self):
    return self.__emails

  @property
  def fullname(self):
    return self.__fullname

  @property
  def public_key(self):
    return self.__public_key

  @property
  def private_key(self):
    return self.__private_key

  @property
  def password_hash(self):
    return self.__password_hash

  @property
  def dropbox_accounts(self):
    return self.__dropbox_accounts

  @property
  def google_accounts(self):
    return self.__google_accounts

  @property
  def gcs_accounts(self):
    return self.__gcs_accounts

  @property
  def ldap_dn(self):
    return self.__ldap_dn

  @property
  def description(self):
    return self.__description

  def __eq__(self, other):
    if self.name != other.name or self.public_key != other.public_key:
      return False
    return True

class Optional:
  pass

class Entity(type):

  def __new__(self, name, superclasses, content,
              insert = None,
              update = None,
              hasher = None,
              fields = {}):
    self_type = None
    content['fields'] = fields
    content['__hash__'] = lambda self: hasher(self)
    # Init
    def __init__(self, beyond, **kwargs):
      self.__beyond = beyond
      for f, default in fields.items():
        v = kwargs.pop(f, None)
        if v is None:
          v = default
        if f == 'name' and not isinstance(v, Optional):
          test_name = v.split('/')[-1]
          validation.Name(name, 'name')(test_name)
        if f == 'description' and not isinstance(v, Optional):
          validation.Description(name, 'description')(v)
        setattr(self, '_%s__%s' % (name, f), v)
        setattr(self, '_%s__%s_original' % (name, f), deepcopy(v))
      if kwargs:
        raise TypeError(
          '__init__() got an unexpected keyword argument %r' %
          next(iter(kwargs)))
    content['__init__'] = __init__
    # JSON
    def json(self):
      assert all(getattr(self, m) is not None for m in fields)
      return {
        m: getattr(self, m) for m in fields
        if not isinstance(getattr(self, m), Optional)
      }
    content['json'] = json
    def from_json(beyond, json):
      missing = next((f for f, d in fields.items()
                      if json.get(f) is None and fields[f] is None),
                     None)
      if missing is not None:
        raise Exception(
          'missing mandatory JSON key for '
          '%s: %s' % (self.__name__, missing))
      body = deepcopy(fields)
      # Replace optionals by 'None'.
      body.update({
        k: None
        for k, v in fields.items() if isinstance(v, Optional)
      })
      body.update({
        k: v
        for k, v in json.items() if k in fields
      })
      return self_type(beyond, **body)
    content['from_json'] = from_json
    # Create
    if insert:
      def create(self):
        missing = next((f for f, d in fields.items()
                        if getattr(self, f) is None \
                        and not isinstance(getattr(self, f), Optional) \
                        and d is None),
                       None)
        if missing is not None:
          raise exceptions.MissingField(
            type(self).__name__.lower(), missing)
        getattr(self.__beyond._Beyond__datastore, insert)(self)
      content['create'] = create
    # Save
    if update:
      def save(self):
        diff = {}
        for field in fields:
          v = getattr(self, field)
          original_field = '_%s__%s_original' % (
            self.__class__.__name__, field)
          original = getattr(self, original_field)
          if isinstance(v, dict):
            for k, v in v.items():
              if original.get(k) != v:
                if isinstance(v, Optional):
                  diff.setdefault(field, {})[k] = None
                else:
                  diff.setdefault(field, {})[k] = v
            setattr(self, original_field, deepcopy(v))
          elif original != v:
            if isinstance(v, Optional):
              diff[field] = None
            else:
              diff[field] = v
        updater = getattr(self.__beyond._Beyond__datastore, update)
        updater(self.id, diff)
      content['save'] = save
    def overwrite(self):
      diff = {
        field: getattr(self, field) for field in fields \
        if not isinstance(getattr(self, field), Optional)
      }
      for field, v in diff.items():
        if isinstance(v, Optional):
          del diff[field]
      del diff['name']
      updater = getattr(self.__beyond._Beyond__datastore, update)
      updater(self.id, diff)
      return diff
    content['overwrite'] = overwrite
    # Properties
    def make_getter(f):
      return lambda self: getattr(self, '_%s__%s' % (name, f))
    for f in fields:
      content[f] = property(make_getter(f))
    # Exceptions
    content['Duplicate'] = type(
      'Duplicate',
      (Exception,),
      {'__qualname__': '%s.Duplicate' % name},
    )
    content['NotFound'] = type(
      'NotFound',
      (Exception,),
      {'__qualname__': '%s.NotFound' % name},
    )
    self_type = type.__new__(self, name, superclasses, content)
    return self_type

  def __init__(self, name, superclasses, content,
               insert = None,
               update = None,
               hasher = None,
               fields = []):
    for f in fields:
      content[f] = property(
        lambda self: getattr(self, '_%s__%s' % (name, f)))
    type.__init__(self, name, superclasses, content)

def fields(*args, **kwargs):
  return dict(chain(((k, None) for k in args), kwargs.items()))

class PairingInformation(
    metaclass = Entity,
    insert = 'pairing_insert',
    fields = fields('name', 'passphrase_hash', 'data', 'expiration')):

  @property
  def id(self):
    return self.name

class Network(metaclass = Entity,
              insert = 'network_insert',
              update = 'network_update',
              fields = fields(
                'name', 'owner', 'consensus', 'overlay',
                version = '0.3.0',
                passports = {},
                endpoints = {},
                storages = {},
                admin_keys = {},
                peers = [],
                description = Optional(),
                encrypt_options = {'encrypt_at_rest': True,
                                   'encrypt_rpc': True,
                                   'validate_signatures': True}
                )):

  @property
  def id(self):
    return self.name

  @property
  def owner_name(self):
    return self.name.split('/')[0]

  @property
  def unqualified_name(self):
    return self.name.split('/')[1]

  def __eq__(self, other):
    if self.name != other.name or \
       self.owner != other.owner or \
       self.consensus != other.consensus or \
       self.overlay != other.overlay or \
       self.admin_keys != other.admin_keys:
      return False
    return True

  class Statistics(metaclass = Entity,
                   fields = fields('usage', 'capacity')):
    pass

class Passport(metaclass = Entity,
               fields = fields('user', 'network', 'signature',
                               allow_write = True,
                               allow_storage = True,
                               allow_sign = False,
                               certifier = Optional())):
  pass

class Volume(metaclass = Entity,
             insert = 'volume_insert',
             hasher = lambda v: hash(v.name),
             fields = fields('name', 'network',
                             owner = Optional(),
                             default_permissions = '',
                             mount_options = dict(),
                             description = Optional())):

  @property
  def id(self):
    return self.name

  @property
  def owner_name(self):
    return self.name.split('/')[0]

  @property
  def unqualified_name(self):
    return self.name.split('/')[1]

  def __eq__(self, other):
    if self.name != other.name or self.network != other.network:
      return False
    return True

class Drive(
    metaclass = Entity,
    insert = 'drive_insert',
    update = 'drive_update',
    fields = fields('name', 'owner', 'network','volume', 'description',
                    users = {}, description = Optional())):

  @property
  def id(self):
    return self.name

  @property
  def owner_name(self):
    return self.name.split('/')[0]

  @property
  def unqualified_name(self):
    return self.name.split('/')[1]

  def __eq__(self, other):
    if self.name != other.name or \
       self.network != other.network or \
       self.volume != other.volume or \
       self.description != other.description:
      return False
    return True

  class Invitation(
      metaclass = Entity,
      fields = fields('permissions', 'status', 'create_home')):
    statuses = ['pending', 'ok']

    class AlreadyConfirmed(Exception):
      pass

    class NotInvited(Exception):
      pass

    # XXX: Check that status in in statuses.
    def save(self, beyond, drive, owner, invitee, invitation):
      confirm = not invitation
      plain = not isinstance(invitee, User)
      if plain:
        assert invitation
        assert beyond.is_email(invitee)
      key = invitee if plain else invitee.name
      email = invitee if plain else invitee.email
      if invitation:
        if key in drive.users and drive.users[key] == 'pending':
          return False
        elif drive.users.get(key, None) == 'ok':
          raise AlreadyConfirmed()
      if confirm:
        if key not in drive.users:
          raise NotInvited()
        elif drive.users.get(key, None) == 'ok':
          return False
      drive.users[key] = self.json()
      drive.save()
      variables = {
        'owner': {
          x: getattr(owner, x) for x in ['name', 'email'] },
        'drive':
        { x: getattr(drive, x) for x in ['name', 'description'] },
      }
      if plain:
        variables['invitee'] = { 'email': email }
      else:
        variables['invitee'] = {
          x: getattr(invitee, x) for x in ['name', 'email'] }
        variables['invitee']['avatar'] = '/users/%s/avatar' % key

      variables['owner']['avatar'] = '/users/%s/avatar' % owner.name
      variables['drive']['icon'] = '/drives/%s/icon' % drive.name
      if invitation and email is not None:
        template = \
          'Drive/%sInvitation' % ('' if not plain else 'Plain ')
        beyond.emailer.send_one(
          recipient_email = email,
          recipient_name = key,
          variables = variables,
          **beyond.template(template)
        )
      if confirm and owner.email is not None:
        beyond.emailer.send_one(
          recipient_email = owner.email,
          recipient_name = owner.name,
          variables = variables,
          **beyond.template('Drive/Joined')
        )
      return True

class KeyValueStore(metaclass = Entity,
                    insert = 'key_value_store_insert',
                    hasher = lambda kvs: hash(kvs.name),
                    fields = fields('name', 'network',
                                    owner = Optional(),
                                    description = Optional())):

  @property
  def id(self):
    return self.name

  @property
  def owner_name(self):
    return self.name.split('/')[0]

  @property
  def unqualified_name(self):
    return self.name.split('/')[1]

  def __eq__(self, other):
    if self.name != other.name or self.network != other.network:
      return False
    return True
