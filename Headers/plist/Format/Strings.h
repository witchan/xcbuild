/**
 Copyright (c) 2015-present, Facebook, Inc.
 All rights reserved.

 This source code is licensed under the BSD-style license found in the
 LICENSE file in the root directory of this source tree. An additional grant
 of patent rights can be found in the PATENTS file in the same directory.
 */

#ifndef __plist_Format_Strings_h
#define __plist_Format_Strings_h

#include <plist/Format/Base.h>
#include <plist/Format/Encoding.h>

namespace plist {
namespace Format {

class Strings : public Base<Strings> {
private:
    Encoding _encoding;

private:
    Strings(Encoding encoding);

public:
    inline Encoding encoding() const
    { return _encoding; }

public:
    static Strings Create(Encoding encoding);
};

}
}

#endif  // !__plist_Format_Strings_h
