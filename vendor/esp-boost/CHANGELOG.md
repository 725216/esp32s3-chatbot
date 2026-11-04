# ChangeLog

## v0.6.0 - 2026-05-12

### Enhancements:

* feat(repo): support ESP-IDF v6 by removing the `<6.0` dependency limit
* feat(gitlab): update CI structure for test apps
* feat(test_apps): update test apps for ESP-IDF v6 targets

### Bugfixes:

* fix(thread): avoid `shared_future` get hang after readiness on ESP targets
* fix(thread): fix Boost.Thread move-assignment typo exposed by stricter diagnostics

## v0.5.1 - 2026-03-11

### Bugfixes:

* fix(config): fix build error when lwip is not enabled

## v0.5.0 - 2026-03-10

### Breaking Changes:

* break(repo): upgrade esp-idf dependency to `>= v5.5`
* bread(Kconfig): disable most libs by default

### Enhancements:

* feat(CI): update build and target tests
* feat(config): define `BOOST_THREAD_DONT_PROVIDE_PROMISE_LAZY` by default
* feat(test_apps): update sdkconfig and pytest scripts

### Bugfixes:

* fix(config): use `CONFIG_BOOST_ASIO_ENABLED` instead of `CONFIG_BOOST_THREAD_ENABLED` for `pipe` function
* chore(libs): remove boost.atomic from supported libraries list

## v0.4.2 - 2025-12-5

### Enhancements:

* feat(libs): support assign
* feat(libs): support bimap
* feat(config): avoid TLS compatibility issue with boost::asio on ESP32-P4 (RISC-V)
* feat(test_apps): update test cases for assign
* feat(test_apps): update examples and test cases for bimap
* feat(tools): update run_test_apps.py

## v0.4.1 - 2025-10-30

### Bugfixes:

* feat(config): enable BOOST_THREAD_HAS_PTHREAD_MUTEXATTR_SETTYPE
* fix(thread): skip get_id() in thread_group
* fix(asio): add wrap functions for read, write, close, fcntl
* fix(test_apps): esp32s3 disable psram by default

## v0.4.0 - 2025-10-21

### Breaking Changes:

* break(repo): remove Arduino and Micropython support

### Enhancements:

* feat(Kconfig): add 'BOOST_SYSTEM_ENABLED', 'BOOST_CHRONO_ENABLED', 'BOOST_THREAD_ENABLED', 'BOOST_REGEX_ENABLED', 'BOOST_RANDOM_ENABLED', 'BOOST_GRAPH_ENABLED', 'BOOST_EXCEPTION_ENABLED', 'BOOST_CONTAINER_ENABLED', 'BOOST_TEST_ENABLED'.
* feat(libs): support json, asio

### Bugfixes:

* fix(Kconfig): change BOOST_MATH_ENABLED and BOOST_SERIALIZATION_ENABLED default value to 'n'

## v0.3.1 - 2025-07-28

### Enhancements:

* feat(libs): support format
* feat(test_apps): update examples and test cases for format
* feat(test_apps): update examples and test cases for algorithm
* feat(Kconfig): add 'BOOST_MATH_ENABLED' and 'BOOST_SERIALIZATION_ENABLED', enanle 'COMPILER_CXX_EXCEPTIONS' and 'COMPILER_CXX_RTTI' by default

## v0.3.0 - 2025-06-25

### Breaking Changes:

* break(repo): upgrade esp-idf dependency to >= v5.3

### Enhancements:

* feat(repo): remove cxx compile version
* feat(repo): remove useless action files

## v0.2.0 - 2025-06-18

### Enhancements:

* feat(libs): support graph
* feat(test_apps): update test cases for array and align

## v0.1.1 - 2025-04-25

### Bugfixes:

* fix(thread): disable 'BOOST_THREAD_INTERNAL_CLOCK_IS_MONO' by default

## v0.1.0 - 2025-04-07

### Enhancements:

* feat(libs): support signals2, thread

### Bugfixes:

* fix(ci): update github actions
