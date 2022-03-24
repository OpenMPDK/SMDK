prerequisite:

1. build library
```
$BASEDIR/lib/build_lib.sh smdkmalloc
```

2. build voltdb
- change java version (11.0.11 -> 1.8.0)
- change python version (3.X -> 2.X)
```
sudo update-alternatives --config java
java -version
sudo update-alternatives --config javac
javac -version
sudo update-alternatives --config python
python --version
```

- voltdb build
```
$BASEDIR/lib/build_lib.sh voltdb
```
