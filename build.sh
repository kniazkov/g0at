#  Copyright 2022 Ivan Kniazkov

#  Use of this source code is governed by an MIT-style license
#  that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.

[ -e goat ] && rm goat
mkdir -p build
cd build

[ -e goat ] && rm goat
[ -e unit_testing ] && rm unit_testing

cmake ../src

make goat
if [ -e goat ]
then
    cp goat ..
else
    exit -1
fi

make unit_testing
if [ -e unit_testing ]
then
    ./unit_testing
    testing_result=$?
    exit $testing_result
else
    exit -1
fi
