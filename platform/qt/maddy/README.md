# maddy

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Version: 1.1.2](https://img.shields.io/badge/Version-1.1.2-brightgreen.svg)](https://semver.org/)
[![Travis Build Status](https://travis-ci.org/progsource/maddy.svg?branch=master)](https://travis-ci.org/progsource/maddy)
[![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/04m0lg27kigv1pg8/branch/master?svg=true)](https://ci.appveyor.com/project/progsource/maddy/branch/master)

maddy is a C++ Markdown to HTML **header-only** parser library.

## Supported OS

It actually should work on any OS, that supports the C++14 standard library.

It is tested to work on:

* Linux (gcc)
* OSX (clang)
* Windows (Visual Studio 2017)

## Dependencies

* C++14

## Why maddy?

When I was needing a Markdown parser in C++ I couldn't find any, that was
fitting my needs. So I simply wrote my own one.

## Markdown syntax

The supported syntax can be found in the [definitions docs](docs/definitions.md).

## How to use

To use maddy in your project, simply add the include path of maddy to yours
and in the code, you can then do the following:

```c++
#include <memory>
#include <string>

#include "maddy/parser.h"

std::stringstream markdownInput("");

// config is optional
std::shared_ptr<maddy::ParserConfig> config = std::make_shared<maddy::ParserConfig>();
config->isEmphasizedParserEnabled = true; // default
config->isHTMLWrappedInParagraph = true; // default

std::shared_ptr<maddy::Parser> parser = std::make_shared<maddy::Parser>(config);
std::string htmlOutput = parser->Parse(markdownInput);
```

## How to run the tests

*(tested on Linux with
[git](https://git-scm.com/book/en/v2/Getting-Started-Installing-Git) and
[cmake](https://cmake.org/install/) installed)*

Open your preferred terminal and type:

```shell
git clone https://github.com/progsource/maddy.git
cd maddy
git submodule update --init --recursive
mkdir tmp
cd tmp
cmake ..
make
make test # or run the executable in ../build/MaddyTests
```

## How to contribute

There are different possibilities:

* [Create a GitHub issue](https://github.com/progsource/maddy/issues/new)
* Create a pull request with an own branch (don't forget to put yourself in the
  AUTHORS file)

Please also read [CONTRIBUTING.md](CONTRIBUTING.md).
