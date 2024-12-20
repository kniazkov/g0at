/**
 * @file root_object.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the root singleton object in the Goat programming language.
 * 
 * This file implements the root object in Goat, which is the ultimate prototype 
 * in the prototype chain. It serves as the foundation for all other objects, 
 * with every object in the language having this object as the last in its prototype 
 * chain. The root object has no prototype itself, and its topology is empty.
 * It contains the methods and fields that all objects in Goat share.
 */

#include "object.h"
