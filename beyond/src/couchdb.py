import couchdb
import couchdb.client
import json
import os
import os.path
import requests
import shutil
import subprocess
import tempfile
import time

from functools import partial

import infinit.beyond

class CouchDB:

  def __init__(self, port = 0, directory = None):
    self.beyond = None
    self.__delete = False
    self.__dir = directory
    self.__uri = None
    self.__port = port

  def __path(self, p):
    return '%s/couchdb.%s' % (self.__dir, p)

  def __wait_until(self, msg, fun):
    '''Run the function until it passes.  Be ready to time out.'''
    count = 100
    while not fun():
      count -= 1
      if count:
        time.sleep(0.1)
      else:
        err = ""
        with open(self.__path('stderr')) as f:
          for line in f:
            err += "  couchdb stderr: " + line
        raise RuntimeError("time out waiting for: {}\n"
                           "{}"
                           .format(msg, err))

  def __enter__(self):
    if self.__dir is None:
      self.__dir = tempfile.mkdtemp()
      self.__delete = True
    else:
      try:
        os.makedirs(self.__dir)
      except FileExistsError:
        pass
    config = self.__path('ini')
    pid = self.__path('pid')
    stdout = self.__path('stdout')
    stderr = self.__path('stderr')
    with open(config, 'w') as f:
      conf = '''\
[couchdb]
database_dir = %(root)s/db-data
view_index_dir = %(root)s/db-data
uri_file = %(root)s/couchdb.uri

[httpd]
port = %(port)s

[log]
file = %(root)s/db.log

[query_servers]
python=python3 -m couchdb
''' % {'root': self.__dir, 'port': self.__port}
      print(conf, file = f)
    try:
      os.remove(self.__path('uri'))
    except:
      pass
    subprocess.check_call(
      ['couchdb', '-a', config,
       '-b', '-p', pid, '-o', stdout, '-e', stderr])
    status_cmd = ['couchdb', '-a', config, '-p', pid, '-s']
    self.__wait_until("couchdb is started",
                      lambda: subprocess.call(status_cmd) == 0)
    self.__wait_until("uri file exists",
                      lambda: os.path.exists(self.__path('uri')))
    self.__wait_until("uri file is filled",
                      lambda: os.stat(self.__path('uri')).st_size != 0)
    with open(self.__path('uri'), 'r') as f:
      self.__uri = f.read().strip()
    while True:
      try:
        if requests.get(self.__uri).json()['couchdb'] == 'Welcome':
          break
      except requests.exceptions.ConnectionError:
        time.sleep(0.1)
    return couchdb.Server(self.__uri)

  def __exit__(self, *args, **kwargs):
    subprocess.check_call(['couchdb', '-d', '-p', self.__path('pid')])
    if self.__delete:
      shutil.rmtree(self.__dir)

  @property
  def uri(self):
    return self.__uri

def getsource(f):
  import inspect
  lines = inspect.getsource(f).split('\n')
  pad = 0
  while pad < len(lines[0]) and lines[0][pad] == ' ':
    pad += 1
  return '\n'.join(line[min(len(line), pad):] for line in lines)

class CouchDBDatastore:

  def __init__(self, db):
    self.__couchdb = db
    self.__design('users',
                  updates = [('update', self.__user_update)],
                  views = [
                    ('per_name', self.__user_per_name),
                    ('per_email', self.__user_per_email),
                    ('per_ldap_dn', self.__user_per_ldap_dn),
                    ('per_short_key_hash', self.__user_per_short_key_hash),
                  ])
    self.__design('deleted-users',
                  updates = [],
                  views = [])
    self.__design('pairing',
                  updates = [],
                  views = [])
    self.__design('networks',
                  updates = [('update', self.__network_update)],
                  views = [
                    ('per_invitee_name',
                     self.__networks_per_invitee_name_map),
                    ('per_owner_key',
                     self.__networks_per_owner_key_map),
                    ('per_user_key',
                     self.__networks_per_user_key_map)
                  ],
                  views_with_reduce = [
                    ('stat_view',
                     self.__network_stat_map,
                     self.__network_stat_reduce),
                  ])
    self.__design('volumes',
                  updates = [('update', self.__volume_update)],
                  views = [
                    ('per_network_id',
                     self.__volumes_per_network_id_map),
                    ('per_owner_key',
                     self.__volumes_per_owner_map),
                  ])
    self.__design('drives',
                  updates = [('update', self.__drive_update)],
                  views = [
                    ('per_member_name', self.__drives_per_member_map),
                    ('per_network_id', self.__drives_per_network_id_map),
                    ('per_volume_id', self.__drives_per_volume_id_map),
                  ])
    self.__design('kvs',
                  updates = [('update', self.__key_value_store_update)],
                  views = [
                    ('per_network_id',
                     self.__key_value_stores_per_network_id_map),
                    ('per_owner_key',
                     self.__key_value_stores_per_owner_map),
                  ])

  def __create(self, name):
    try:
      self.__couchdb.create(name)
    except couchdb.http.PreconditionFailed as e:
      if e.args[0][0] == 'file_exists':
        pass
      else:
        raise

  def __design(self,
               category,
               updates,
               views,
               views_with_reduce = []):
    self.__create(category)
    try:
      design = self.__couchdb[category]['_design/beyond']
    except couchdb.http.ResourceNotFound:
      design = couchdb.client.Document()
    views = {
      name : {'map': getsource(view_map)} for name, view_map in views
    }
    views.update({
      name : {'map': getsource(view), 'reduce': getsource(reducer)}
      for name, view, reducer in views_with_reduce
    })
    design.update(
      {
        '_id': '_design/beyond',
        'language': 'python',
        'updates': {
          name: getsource(update) for name, update in updates
        },
        'views': views,
      })
    self.__couchdb[category].save(design)

  ## ---- ##
  ## User ##
  ## ---- ##

  def __user_purge_json(self, user):
    user = dict(user)
    user['id'] = user['_id']
    del user['_id']
    return user

  def user_insert(self, user):
    json = user.json(private = True,
                     hide_confirmation_codes = False)
    json['_id'] = json['name']
    try:
      self.__couchdb['users'].save(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.User.Duplicate()

  def users_fetch(self):
    return (
      self.__user_purge_json(u.doc) for u in
      self.__couchdb['users'].view('_all_docs', include_docs = True)
      if not u.id.startswith('_design/'))

  def user_fetch(self, name):
    try:
      return self.__user_purge_json(self.__couchdb['users'][name])
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.User.NotFound()

  def __user_per_short_key_hash(user):
    from base64 import b64decode
    from hashlib import sha256
    key_hash = sha256(b64decode(user['public_key']['rsa'])).hexdigest()
    yield '#%s' % key_hash[0:6], user

  def user_by_short_key_hash(self, hash):
    rows = self.__couchdb['users'].view('beyond/per_short_key_hash', key = hash)
    if len(rows) == 0:
      raise infinit.beyond.User.NotFound()
    return [r.value for r in rows][0]

  def __user_per_email(user):
    for email, confirmation in user.get('emails', {}).items():
      yield email, user
    if 'email' in user and user['email'] not in user.get('emails', {}).keys():
      yield user['email'], user

  def users_by_email(self, email):
    rows = self.__couchdb['users'].view('beyond/per_email', key = email)
    return [r.value for r in rows]

  def __user_per_name(user):
    yield user['name'], user

  def __user_per_ldap_dn(user):
    if 'ldap_dn' in user:
      yield user['ldap_dn'], user
    else:
      yield None, user # Fixme what to do in that case?

  def user_by_ldap_dn(self, dn):
    rows = self.__couchdb['users'].view('beyond/per_ldap_dn', key = dn)
    if len(rows) == 0:
      raise infinit.beyond.User.NotFound()
    return [r.value for r in rows][0]

  def user_update(self, id, diff = {}):
    args = {
      name: json.dumps(value)
      for name, value in diff.items()
      if value is not None
    }
    try:
      self.__couchdb['users'].update_doc(
        'beyond/update',
        id,
        **args
      )
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.User.NotFound()

  def __user_update(user, req):
    if user is None:
      return [
        None,
        {
          'code': 404,
        }
      ]
    import json
    update = {
      name: json.loads(value)
      for name, value in req['query'].items()
    }
    for type in ['dropbox', 'google', 'gcs']:
      for id, account in update.get('%s_accounts' % type, {}).items():
        accounts = user.setdefault('%s_accounts' % type, {})
        if account is None:
          if id in accounts:
            del accounts[id]
        else:
          user.setdefault('%s_accounts' % type, {})[id] = account
    for email, confirmation in update.get('emails', {}).items():
      user.setdefault('emails', {})[email] = confirmation
    return [user, {'json': json.dumps(update)}]

  def user_delete(self, name):
    doc = self.__couchdb['users'][name]
    self.__couchdb['users'].delete(doc)

  def user_deleted_get(self, name):
    try:
      return self.__couchdb['deleted-users'][name]
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.User.NotFound()

  def user_deleted_add(self, name):
    user = self.user_fetch(name)
    try:
      doc = self.user_deleted_get(name)
      doc['versions'].append(user)
      self.__couchdb['deleted-users'].save(doc)
    except infinit.beyond.User.NotFound:
      doc = {'_id': name, 'versions': [user]}
      self.__couchdb['deleted-users'].save(doc)

  def __rows_to_networks(self, rows):
    network_from_db = infinit.beyond.Network.from_json
    return list(map(lambda r: network_from_db(self.beyond, r.value), rows))

  def invitee_networks_fetch(self, invitee):
    rows = self.__couchdb['networks'].view('beyond/per_invitee_name',
                                           key = invitee.name)
    return self.__rows_to_networks(rows)

  def owner_networks_fetch(self, owner):
    rows = self.__couchdb['networks'].view('beyond/per_owner_key',
                                           key = owner.public_key)
    return self.__rows_to_networks(rows)

  def user_networks_fetch(self, user):
    return (doc.value for doc in
            self.__couchdb['networks'].view('beyond/per_user_key',
                                            key = user.public_key))

  def network_stats_fetch(self, network):
    rows = self.__couchdb['networks'].view('beyond/stat_view',
                                           key = network.name)
    rows = list(rows)
    res = {'capacity': 0, 'usage': 0}
    if len(rows) > 0:
      res = rows[0].value
    return res

  def owner_volumes_fetch(self, owner):
    rows = self.__couchdb['volumes'].view('beyond/per_owner_key',
                                          key = owner.public_key)
    return self.__rows_to_networks(rows)

  ## ------- ##
  ## Pairing ##
  ## ------- ##

  def __format_pairing_information(self, pairing_information):
    json = pairing_information.json()
    # XXX: Remove name.
    json['_id'] = pairing_information.name
    from datetime import datetime
    json['expiration'] = time.mktime(json['expiration'].timetuple())
    return json

  def pairing_insert(self, pairing_information):
    json = self.__format_pairing_information(pairing_information)
    try:
      self.__couchdb['pairing'].save(json)
    except couchdb.ResourceConflict:
      self.pairing_delete(pairing_information.name)
      self.pairing_insert(pairing_information)

  def pairing_fetch(self, owner):
    try:
      json = self.__couchdb['pairing'][owner]
      del json['_id']
      from datetime import datetime
      json['expiration'] = datetime.fromtimestamp(json['expiration'])
      return json
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.PairingInformation.NotFound()

  def pairing_delete(self, owner):
    try:
      json = self.__couchdb['pairing'][owner]
      self.__couchdb['pairing'].delete(json)
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.PairingInformation.NotFound()

  ## ------- ##
  ## Network ##
  ## ------- ##

  def network_insert(self, network):
    json = network.json()
    json['_id'] = network.name
    try:
      self.__couchdb['networks'].save(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.Network.Duplicate()

  def network_fetch(self, owner, name):
    try:
      return self.__couchdb['networks']['%s/%s' % (owner, name)]
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.Network.NotFound()

  def network_delete(self, owner, name):
    try:
      json = self.__couchdb['networks']['%s/%s' % (owner, name)]
      self.__couchdb['networks'].delete(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.Network.Duplicate()

  def network_update(self, id, diff):
    args = {
      name: json.dumps(value)
      for name, value in diff.items()
      if value is not None
    }
    try:
      self.__couchdb['networks'].update_doc(
        'beyond/update',
        id,
        **args
      )
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.Network.NotFound()
    except couchdb.http.ServerError as e:
      if e.args[0][0] == 402:
        raise infinit.beyond.Network.PaymentRequired()
      raise e

  def networks_volumes_fetch(self, networks):
    return (doc.value for doc in
            self.__couchdb['volumes'].view(
              'beyond/per_network_id',
              keys = list(map(lambda n: n.id, networks))))

  def user_volumes_fetch(self, user):
    return (doc.value for doc in
            self.__couchdb['volumes'].view(
              'beyond/per_owner_key', key = user.public_key))

  def networks_key_value_stores_fetch(self, networks):
    return (doc.value for doc in
            self.__couchdb['kvs'].view(
              'beyond/per_network_id',
              keys = list(map(lambda n: n.id, networks))))

  def user_key_value_stores_fetch(self, user):
    return (doc.value for doc in
            self.__couchdb['kvs'].view(
              'beyond/per_owner_key', key = user.public_key))

  def __network_update(network, req):
    if network is None:
      return [
        None,
        {
          'code': 404,
        }
      ]
    import json
    update = {
      name: json.loads(value)
      for name, value in req['query'].items()
    }
    for user, passport in update.get('passports', {}).items():
      if passport is None:
        network.setdefault('passports', {}).pop(user)
      else:
        network.setdefault('passports', {})[user] = passport
    for user, node in update.get('endpoints', {}).items():
      for node, endpoints in node.items():
        n = network.setdefault('endpoints', {})
        u = n.setdefault(user, {})
        if endpoints is None:
          u.pop(node, None)
          if not u:
            n.pop(user)
        else:
          u[node] = endpoints
    for user, node in update.get('storages', {}).items():
      network.setdefault('storages', {})[user] = node
    for field in ['passports', 'endpoints', 'storages']:
      if field in update:
        del update[field]
    network.update(update)
    return [network, {'json': json.dumps(update)}]

  def __networks_per_invitee_name_map(network):
    for p in network.get('passports', {}):
      yield p, network

  def __networks_per_owner_key_map(network):
    yield network['owner'], network

  def __networks_per_user_key_map(network):
    for elem in network.get('passports', {}).values():
      yield elem['user'], network
    yield network['owner'], network

  def __network_stat_map(network):
    for _, nodes in network.get('storages', {}).items():
      for _, stat in nodes.items():
        yield network['name'], stat

  def __network_stat_reduce(keys, values, rereduce):
    res = {'capacity': 0, 'usage': 0}
    for v in values:
      res['capacity'] += v['capacity']
      res['usage'] += v['usage']
    return res

  ## ------ ##
  ## Volume ##
  ## ------ ##

  def volume_insert(self, volume):
    json = volume.json()
    json['_id'] = volume.name
    try:
      self.__couchdb['volumes'].save(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.Volume.Duplicate()

  def volume_fetch(self, owner, name):
    try:
      json = self.__couchdb['volumes']['%s/%s' % (owner, name)]
      return json
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.Volume.NotFound()

  def volume_delete(self, owner, name):
    try:
      json = self.__couchdb['volumes']['%s/%s' % (owner, name)]
      self.__couchdb['volumes'].delete(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.Volume.Duplicate()

  def __volume_update(volume, req):
    if volume is None:
      return [
        None,
        {
          'code': 404,
        }
      ]
    import json
    update = {
      name: json.loads(value)
      for name, value in req['query'].items()
    }
    return [volume, {'json': json.dumps(update)}]

  def __volumes_per_network_id_map(volume):
    yield volume['network'], volume

  def __volumes_per_owner_map(volume):
    yield volume.get('owner', None), volume

  ## ----- ##
  ## Drive ##
  ## ----- ##

  def drive_insert(self, drive):
    json = drive.json()
    json['_id'] = drive.id
    try:
      self.__couchdb['drives'].save(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.Drive.Duplicate()

  def drive_fetch(self, owner, name):
    try:
      json = self.__couchdb['drives']['%s/%s' % (owner, name)]
      drive = infinit.beyond.Drive.from_json(self.beyond, json)
      return drive
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.Drive.NotFound()

  def drive_update(self, id, diff):
    args = {
      name: json.dumps(value)
      for name, value in diff.items()
      if value is not None
    }
    try:
      self.__couchdb['drives'].update_doc(
        'beyond/update',
        id,
        **args
      )
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.Drive.NotFound()

  def __drive_update(drive, req):
    if drive is None:
      return [
        None,
        {
          'code': 404,
        }
      ]
    import json
    update = {
      name: json.loads(value)
      for name, value in req['query'].items()
    }
    for user, value in update.get('users', {}).items():
      if value is None and user in update.get('users', {}):
        del drive['users'][user]
      else:
        drive.setdefault('users', {})[user] = value
    update.pop('users', None)
    return [drive, {'json': json.dumps(update)}]

  def drive_delete(self, owner, name):
    try:
      json = self.__couchdb['drives']['%s/%s' % (owner, name)]
      self.__couchdb['drives'].delete(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.Drive.Duplicate()

  def user_drives_fetch(self, name):
    drive_from_db = partial(infinit.beyond.Drive.from_json, self.beyond)
    rows = self.__couchdb['drives'].view('beyond/per_member_name',
                                         key = name)
    return list(map(lambda x: drive_from_db(x.value), rows))

  def network_drives_fetch(self, name):
    rows = self.__couchdb['drives'].view('beyond/per_network_id', key = name)
    drive_from_db = infinit.beyond.Drive.from_json
    return list(map(lambda r: drive_from_db(self.beyond, r.value), rows))

  def volume_drives_fetch(self, name):
    rows = self.__couchdb['drives'].view('beyond/per_volume_id', key = name)
    drive_from_db = infinit.beyond.Drive.from_json
    return list(map(lambda r: drive_from_db(self.beyond, r.value), rows))

  def __drives_per_member_map(drive):
    for elem in drive.get('users', {}).keys():
      yield elem, drive
    yield drive['owner'], drive

  def __drives_per_network_id_map(drive):
    yield drive['network'], drive

  def __drives_per_volume_id_map(drive):
    yield drive['volume'], drive

  ## --------------- ##
  ## Key Value Store ##
  ## --------------- ##

  def key_value_store_insert(self, kvs):
    json = kvs.json()
    json['_id'] = kvs.name
    try:
      self.__couchdb['kvs'].save(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.KeyValueStore.Duplicate()

  def key_value_store_fetch(self, owner, name):
    try:
      json = self.__couchdb['kvs']['%s/%s' % (owner, name)]
      return json
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.KeyValueStore.NotFound()

  def key_value_store_delete(self, owner, name):
    try:
      json = self.__couchdb['kvs']['%s/%s' % (owner, name)]
      self.__couchdb['kvs'].delete(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.KeyValueStore.Duplicate()

  def __key_value_store_update(kvs, req):
    if kvs is None:
      return [
        None,
        {
          'code': 404,
        }
      ]
    import json
    update = {
      name: json.loads(value)
      for name, value in req['query'].items()
    }
    return [kvs, {'json': json.dumps(update)}]

  def __key_value_stores_per_network_id_map(kvs):
    yield kvs['network'], kvs

  def __key_value_stores_per_owner_map(kvs):
    yield kvs.get('owner', None), kvs
