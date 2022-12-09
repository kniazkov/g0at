#  Copyright 2022 Ivan Kniazkov

#  Use of this source code is governed by an MIT-style license
#  that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.

[ -e goat ] && rm goat
mkdir -p build
cd build
[ -e goat ] && rm goat
cmake ../src
make goat
make unit_testing
[ -e goat ] && cp goat ..
cd ..
