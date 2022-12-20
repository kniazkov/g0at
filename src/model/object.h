/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace goat {

    class object;

    /**
     * @brief Primitive data processed by the interpreter
     * 
     * Primitive data types, regardless of their size, always occupy one "cell" 
     * (in most cases 8 bytes). This makes it easier to operate the stack. Therefore, all these 
     * types are combined into the union. The interpretation of the data (which type in particular 
     * case) depends on the object processing the data.
     */
    union raw_data {
        int64_t int_value;
        double double_value;
    };

    /**
     * @brief Variable that stores the data
     * 
     * A variable consists of two components: the processing object (can't be null) and the data.
     * If a variable stores a primitive (e.g., an integer), then the object that processes it is
     * static (i.e., not stored in the heap). This allows not to use dynamic memory for primitives,
     * which greatly speeds up the interpreter. If the data are not primitive, then they are stored
     * inside the object, and the memory of such an object is allocated from the heap.
     */
    struct variable {
        object *obj;
        raw_data data;

        /**
         * @brief Increases the counter of objects referring to processing object
         *
         * This method is used by the garbage collector to count objects.
         */
        inline void add_reference();

        /**
         * @brief Decreases the counter of objects referring to processing object
         *
         * This method is used by the garbage collector to count objects.
         */
        inline void release();

        /**
         * @brief Returns the string representation of the variable, which is used for printing
         *   and debugging purposes
         * @return A string representation of the variable 
         */
        inline std::wstring to_string() const;

        /**
         * @brief Represents the variable in Goat notation, that is, as a string that can be
         *   transformed back into a Goat object
         * @return A string representation of the variable in Goat notation
         */
        inline std::wstring to_string_notation() const;
    };

    /**
     * @brief Defines kinds of objects for further separation of objects in a comparison
     * 
     * Each object can contain child objects. They are stored in a map, whose key is also objects
     * (since almost all Goat entities are objects). We need to compare objects in order to place
     * them in the map. A simple comparison by memory address is not suitable, because,
     * for example, two identical strings can have two different addresses in memory, but
     * in terms of map it is the same key. Accordingly, we need to compare the contents of objects,
     * but we cannot compare objects of different kinds (for example, an integer and a string).
     * So we divide the objects into families, and inside the objects of the same type (family) we
     * can compare the contents.
     */
    enum object_type {
        generic = 1,
        string,
        number
    };

    /**
     * @brief The basic essence of the Goat object-oriented language
     * 
     * Almost everything in the Goat programming language - data, functions, operators,
     * syntactic constructs, scopes, and so on - are objects. An object can contain other objects,
     * as well as be a prototype for other objects.
     */
    class object {
    public:
        /**
         * @brief Constructor
         */
        object();

        /**
         * @brief Destructor
         */
        virtual ~object();

        /**
         * @brief Increases the counter of objects referring to this object
         * 
         * This method is used by the garbage collector to count objects.
         */
        virtual void add_reference() = 0;

        /**
         * @brief Decreases the counter of objects referring to this object
         * 
         * This method is used by the garbage collector to count objects.
         */
        virtual void release() = 0;

        /**
         * @brief Determines if the object is static or not
         * @return <code>true</code> if the object is static (i.e. not counted by the
         * garbage collector), or <code>false</code> otherwise
         */
        virtual bool is_static() = 0;

        /**
         * @brief Returns the type of the object
         * @return The type of the object
         */
        virtual object_type get_type() const = 0;

        /**
         * @brief Compares the object to another
         * @param other Pointer to another object
         * 
         * Compares the object to another object and makes some decision (by some criteria) that
         * the object is "less" than the other
         */
        virtual bool less(const object* const other) const;

        /**
         * @brief Returns the prototype of this object by index
         * @param index The index
         * @return The prototype of this object or <code>nullptr</code> if there is no prototype
         *   for the specified index
         */
        virtual object *get_prototype(unsigned int index) const;

        /**
         * @brief Returns the first prototype of this object
         * @return The first prototype of this object
         */
        virtual object *get_first_prototype() const;

        /**
         * @brief Returns the number of prototypes of this object
         * @return The number of prototypes of this object
         */
        virtual unsigned int get_number_of_prototypes() const;

        /**
         * @brief Determines that the object is inherited from another object
         * @param proto A possible prototype of the object
         * @return <code>true</code> if the object has the specified prototype,
         *    and <code>false</code> otherwise 
         */
        virtual bool is_instance_of(const object *proto) const;

        /**
         * @brief Returns the string representation of the object, which is used for printing
         *   and debugging purposes
         * @param var Pointer to a variable to be handled (only for objects
         *   that do not store data themselves)
         * @return A string representation of the object 
         */
        virtual std::wstring to_string(const variable* var) const;

        /**
         * @brief Represents the object in Goat notation, that is, as a string that can be
         *   transformed back into a Goat object
         * @param var Pointer to a variable to be handled (only for objects
         *   that do not store data themselves)
         * @return A string representation of the object in Goat notation
         */
        virtual std::wstring to_string_notation(const variable* var) const = 0;

        /**
         * @brief Sets the attribute (key-value pair)
         * @param key The key
         * @param value The value
         */
        virtual void set_attribute(object *key, variable &value) = 0;

        /**
         * @brief Sets the attribute (key-value pair), where value is an object
         * @param key The key
         * @param value The value
         */
        void set_attribute(object *key, object* value) {
            variable var = {0};
            var.obj = value;
            set_attribute(key, var);
        }

        /**
         * @brief Searches for an attribute in the object and its prototypes by the key
         * @param key The key
         * @return Pointer to value or <code>nullptr</code> if there is no such attribute 
         */
        virtual variable * get_attribute(object *key);

        /**
         * @brief Searches for an attribute that belongs to this object,
         *   but not to its prototypes, by the key
         * @param key The key
         * @return Pointer to value or <code>nullptr</code> if there is no such attribute 
         */
        virtual variable * get_own_attribute(object *key);

        /**
         * @brief Retrieves the string value of the object
         * @param value_ptr Pointer to retrievable value
         * @return <code>true</code> if the object is a string (value can be retrieved),
         *   or <code>false</code> otherwise
         */
        virtual bool get_string_value(std::wstring* const value_ptr) const;
    
        /**
         * @brief Retrieves the real value of the object
         * @param var Pointer to a variable to be handled (only for objects
         *   that do not store data themselves)
         * @param value_ptr Pointer to retrievable value
         * @return <code>true</code> if the object is a string (value can be retrieved),
         *   or <code>false</code> otherwise
         */
        virtual bool get_real_value(const variable* var, double* const value_ptr) const;
        
    private:
        /**
         * @brief Privte copy constructor to prevent copying
         */
        object(const object &) {
        }

        /**
         * @brief Privte assignment operator to prevent copying
         */
        void operator=(const object &) {
        }
    };

    /* ----------------------------------------------------------------------------------------- */

    void variable::add_reference() {
        obj->add_reference();
    }

    void variable::release() {
        obj->release();
    }

    std::wstring variable::to_string() const {
        return obj->to_string(this);
    }

    std::wstring variable::to_string_notation() const {
        return obj->to_string_notation(this);
    }

    object * get_empty_object();
}
