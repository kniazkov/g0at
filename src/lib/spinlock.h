/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <atomic>

namespace goat {

    /**
     * @brief Lightweight mutex
     */
    class spinlock {
        std::atomic_flag locked = ATOMIC_FLAG_INIT ;
    
    public:
        /**
         * @brief Blocks other threads
         * 
         * So that only the current thread will run after this method is called
         */
        void lock() {
            while (locked.test_and_set(std::memory_order_acquire)) {
            }
        }

        /**
         * @brief Unblocks other threads
         */
        void unlock() {
            locked.clear(std::memory_order_release);
        }
    };
}
