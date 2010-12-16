import drake, os

run = False
deps = False
depname = 'drake.test.depname'
dyndeps = False
builder = None

class DynNode(drake.Node):

  pass

def handler(b, path, type, data):

  global dyndeps
  global builder
  dyndeps = True
  assert b == builder
  assert path == 'src'
  assert type == DynNode
  return drake.node(path, type)

drake.Builder.register_deps_handler(depname, handler)

class DynBuilder(drake.Builder):

  def __init__(self, dst):

    drake.Builder.__init__(self, [], [dst])
    self.dst = dst

  def dependencies(self):

    global deps
    deps = True
#    self.src = drake.node('src', DynNode)
    self.src = DynNode('src')
    self.add_dynsrc(depname, self.src)

  def execute(self):

    global run
    os.system('cp %s %s' % (self.src, self.dst))
    run = True
    return True

def configure():

  global builder
  dst = drake.Node('dst')
  builder = DynBuilder(dst)
