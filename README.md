# network-bfd-sample

Incomplete Bidirectional Forwarding Detection's implementation

+ minimal implemented 
+ available switch
  + juniper XX
+ multi implemented.
  + c
    + shared memory map
  + cpp(boost)
    + std::map
  + c with netmap
    + redirect only.


## bfdd

bfdd (bidirectional forwarding detection) service.
implemented on bsd socket.
c compile.

## bfdd_cpp

bfdd service , implemented on boost.

## bfdd_netmap

bfdd service implemented on netmap redirect mode.



## download

```
git clone https://github.com/xflagstudio/network-bfd-sample.git

```

## prepare

+ installed netmap nic driver.
  + https://github.com/luigirizzo/netmap


## compile/link

```
cd bfd/
mkdir ./build
cd ./build
cmake ../
make
```
