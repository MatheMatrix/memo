#! /bin/bash

set -e

rootdir=$1
nodes=$2
k=$3
replicas=3
observers=2
user=test
port_base=5050
nports=0 # allocate fixed port port_base+i to the first nports nodes

network_args="--kelips  --k $k --replication-factor $replicas"
#network_args="--kademlia"

# port(id, port_base, nports
function port {
  if test $1 -le $3; then echo "--port " $(($2 + $1)); fi
}

cd $rootdir

# Generate one main user $user, one user per node, and import $user in each
# also import all users in 0
mkdir store0
mkdir conf0
MEMO_HOME=$rootdir/conf0 infinit-user --create --name $user
exported_user=$(MEMO_HOME=$rootdir/conf0 infinit-user --export --name $user)
for i in $(seq 1 $nodes); do
  mkdir store$i
  mkdir conf$i
  MEMO_HOME=$rootdir/conf$i infinit-user --create --name $user$i
  echo $exported_user | MEMO_HOME=$rootdir/conf$i infinit-user --import
  MEMO_HOME=$rootdir/conf$i infinit-user --export --name $user$i | MEMO_HOME=$rootdir/conf0 infinit-user --import
done

# Generate storages
for i in $(seq 0 $nodes); do
  MEMO_HOME=$rootdir/conf$i infinit-storage --create --capacity 2000000000 --name storage --filesystem --path $rootdir/store$i
done

# Generate overlay network and export structure
MEMO_HOME=$rootdir/conf0 infinit-network --create --storage storage --name kelips --port $port_base --as $user $network_args
exported_network=$(MEMO_HOME=$rootdir/conf0 infinit-network --export --as $user --name kelips)

# import and join network
for i in $(seq 1 $nodes); do
  echo $exported_network | MEMO_HOME=$rootdir/conf$i infinit-network --import
  MEMO_HOME=$rootdir/conf0 infinit-passport --create --as $user --network kelips --user $user$i --output - \
  | MEMO_HOME=$rootdir/conf$i infinit-passport --import
  MEMO_HOME=$rootdir/conf$i infinit-network --link --as $user$i --name $user/kelips --storage storage $(port $i $port_base $nports)
done

#observers
for i in $(seq 0 $observers); do
  mkdir observer_conf_$i
  mkdir observer_mount_$i
  MEMO_HOME=$rootdir/observer_conf_$i infinit-user --create --name obs$i
  echo $exported_user | MEMO_HOME=$rootdir/observer_conf_$i infinit-user --import
  MEMO_HOME=$rootdir/observer_conf_$i infinit-user --export --name obs$i | MEMO_HOME=$rootdir/conf0 infinit-user --import
  echo $exported_network | MEMO_HOME=$rootdir/observer_conf_$i infinit-network --import

  MEMO_HOME=$rootdir/conf0 infinit-passport --create --as $user --network kelips --user obs$i --output - \
  | MEMO_HOME=$rootdir/observer_conf_$i infinit-passport --import
  MEMO_HOME=$rootdir/observer_conf_$i infinit-network --link --as obs$i --name $user/kelips
done

# create volume
MEMO_HOME=$rootdir/conf0 infinit-volume --create --name kelips \
  --as $user --network kelips


exported_volume=$(MEMO_HOME=$rootdir/conf0 infinit-volume --export --as $user --name kelips)
for i in $(seq 0 $observers); do
  echo $exported_volume | MEMO_HOME=$rootdir/observer_conf_$i infinit-volume --import --mountpoint observer_mount_$i
done

echo '#!/bin/bash' > $rootdir/run-nodes.sh
chmod a+x $rootdir/run-nodes.sh
echo "MEMO_HOME=$rootdir/conf0 infinit-volume --run --as $user --name $user/kelips --mountpoint mount_0 --async --cache --allow-root-creation &" >> $rootdir/run-nodes.sh
echo 'sleep 2' >> $rootdir/run-nodes.sh
for i in $(seq 1 $nodes); do
  echo "MEMO_HOME=$rootdir/conf$i infinit-network --run --as $user$i --name $user/kelips --peer 127.0.0.1:$port_base &" >> $rootdir/run-nodes.sh
done

for i in $(seq 0 $observers); do
  echo "MEMO_HOME=$rootdir/observer_conf_$i infinit-volume --run --mountpoint observer_mount_$i --as obs$i --name $user/kelips --peer 127.0.0.1:$port_base --cache --async" > $rootdir/run-volume-$i.sh
  chmod a+x $rootdir/run-volume-$i.sh
done
