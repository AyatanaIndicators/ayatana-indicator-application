# Build and installation instructions

## Compile-time build dependencies

 - cmake (>= 3.13)
 - cmake-extras
 - glib-2.0 (>= 2.58)
 - ayatana-indicator3-0.4 (>= 0.6.2)
 - gtk+-3.0 (>= 3.24)
 - ayatana-appindicator3-0.1 (>= 0.5.5)
 - dbus-glib-1 (>=0.110)
 - systemd
 - intltool
 - gtest (>= 1.6.0)
 - gmock
 - gcovr (>= 2.4)
 - lcov (>= 1.9)

## For end-users and packagers

```
cd ayatana-indicator-application-X.Y.Z
mkdir build
cd build
cmake ..
make
sudo make install
```
## For testers - unit tests only

```
cd ayatana-indicator-application-X.Y.Z
mkdir build
cd build
cmake .. -DENABLE_TESTS=ON
make
make test
```
## For testers - both unit tests and code coverage

```
cd ayatana-indicator-application-X.Y.Z
mkdir build
cd build
cmake .. -DENABLE_COVERAGE=ON
make
make test
make coverage-html
```
**The install prefix defaults to `/usr`, change it with `-DCMAKE_INSTALL_PREFIX=/some/path`**
