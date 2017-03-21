# smmr

The smmr library allows more than 10K data sharing among multiple processes.
Ipc interface uses shared memory.
at 4 processes in parallel access, over 70 KTPS by new addition processing, over 500 KTPS by search processing.

In order to share large amounts of small data among parallel processes, update and search at high speed, following implementation design is applied.

+ small lock granularity.
  + hi parallel processing.
+ persistent shared data.
  + both index area and data area are persistent.

## memory fragment

index is implemented by th used ,free,resusalbe area, each bidirectional ring.
by suppressing the generation of memory fragments,latency deterioration during long-term operation is prevented.


to use for session management of bfd protocol (RFC 5880/5881),
made a library of in memory Database implemented for nginx extension several years ago.

## compile/link
```
cd smmr/
mkdir ./build
cd ./build
cmake ../
make
```
## run test
```
make test
```

## run test with infolog
```
 ./test/utsmmr
```

## sample performance

### osx(mbp 4cpu/16GB)

```
++++results++++------+------------------+
|     operation      |   TPS            |
+--------------------+------------------+
|  STORE_AND_SEARCH  |       33286.9773 |
|  STORE             |       97613.5792 |
|  SEARCH            |      587513.8954 |
<<<< 1 process  +----+------------------+


++++results++++-------+--------------------+------------------+
| process idx | count |     operation      |   TPS            |
+-------------+-------+--------------------+------------------+
|          0  |  8192 |  STORE             |       16635.9008 |
|          0  |  8192 |  SEARCH            |      203700.0199 |
|          1  |  8192 |  STORE             |       19740.6153 |
|          1  |  8192 |  SEARCH            |      221867.1289 |
|          2  |  8192 |  STORE             |       16476.5322 |
|          2  |  8192 |  SEARCH            |      195089.4239 |
|          3  |  8192 |  STORE             |       16368.7443 |
|          3  |  8192 |  SEARCH            |      201351.8496 |
+<< 4 process +-------+--------------------+------------------+
+------------------------------------------+------------------+
| STORE                                    |       69221.7926 |
+------------------------------------------+------------------+
| SEARCH                                   |      822008.4223 |
+------------------------------------------+------------------+

```

### linux(debian):56CPU/Xeon/E5-2680/2.40G

```
++++results++++------+------------------+
|     operation      |   TPS            |
+--------------------+------------------+
|  STORE_AND_SEARCH  |       48650.9199 |
|  STORE             |      107089.1669 |
|  SEARCH            |      917614.1137 |
<<<< 1 process  +----+------------------+

++++results++++-------+--------------------+------------------+
| process idx | count |     operation      |   TPS            |
+-------------+-------+--------------------+------------------+
|          0  |  8192 |  STORE             |       20627.5904 |
|          0  |  8192 |  SEARCH            |      137891.5653 |
|          1  |  8192 |  STORE             |       17318.4334 |
|          1  |  8192 |  SEARCH            |      120874.0944 |
|          2  |  8192 |  STORE             |       20514.7212 |
|          2  |  8192 |  SEARCH            |      121573.6907 |
|          3  |  8192 |  STORE             |       17806.9150 |
|          3  |  8192 |  SEARCH            |      136999.1304 |
+<< 4 process +-------+--------------------+------------------+
+------------------------------------------+------------------+
| STORE                                    |       76267.6599 |
+------------------------------------------+------------------+
| SEARCH                                   |      517338.4807 |
+------------------------------------------+------------------+


```