//          Copyright John W. Wilkinson 2007 - 2011
// Distributed under the MIT License, see accompanying file LICENSE.txt

// json spirit version 4.05

#include "value_test.h"
#include "writer_test.h"
#include "reader_test.h"
#include "stream_reader_test.h"

#include <string>
#include <iostream>

using namespace std;
using namespace json_spirit;

int main()
{
    test_value();
    test_writer();
    test_reader();
    test_stream_reader();

    cout << "all tests passed" << endl;
    return 0;
}
