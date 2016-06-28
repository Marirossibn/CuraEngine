/** Copyright (C) 2016 Ultimaker - Released under terms of the AGPLv3 License */
#ifndef UTILS_OPTIONAL_H
#define UTILS_OPTIONAL_H


#include "intpoint.h"
#include "polygon.h"


namespace std
{
    
    
/*!
 * optional value
 * 
 * Adaptation of std::experimental::optional
 * See http://en.cppreference.com/w/cpp/utility/optional
 * 
 * This implementation *does* allocate on the heap and is therefore not completely according to spec.
 */
template<typename T>
class optional
{
    T* instance;
public:
    optional() //!< create an optional value which is not instantiated
    : instance(nullptr)
    {
    }
    optional(const optional& other)
    {
        if (other.instance)
        {
            instance = new T(*other.instance);
        }
        else
        {
            instance = nullptr;
        }
    }
    optional(optional&& other)
    : instance(other.instance)
    {
        other.instance = nullptr;
    }
    template<class... Args>
    constexpr explicit optional(bool not_used, Args&&... args )
    : instance(new T(args...))
    {
    }
    ~optional()
    {
        if (instance)
        {
            delete instance;
        }
    }
    optional& operator=(void* null_ptr)
    {
        instance = nullptr;
    }
    optional& operator=(const optional& other)
    {
        if (instance)
        {
            delete instance;
            if (other.instance)
            {
                *instance = *other.instance;
            }
            else
            {
                instance = nullptr;
            }
        }
        else
        {
            if (other.instance)
            {
                instance = new T(other.instance);
            }
            else
            {
                instance = nullptr;
            }
        }
    }
    optional& operator=(optional&& other)
    {
        instance = other.instance;
        other.instance = nullptr;
    }
    optional& operator=(T&& value)
    {
        if (instance)
        {
            *instance = value;
        }
        else
        {
            instance = new T(value);
        }
    }
    constexpr T* operator->()
    {
        return instance;
    }
    constexpr T& operator*() &
    {
        return *instance;
    }
    constexpr explicit operator bool() const
    {
        return instance;
    }
    constexpr T& value() &
    {
        return *instance;
    }
    constexpr T value_or(T&& default_value) const&
    {
        if (instance)
        {
            return *instance;
        }
        else
        {
            return default_value;
        }
    }
    void swap(optional& other)
    {
        std::swap(instance, other.instance);
    }
    template<class... Args>
    void emplace(Args&&... args)
    {
        if (instance)
        {
            *instance = T(args...);
        }
        else
        {
            instance = new T(args...);
        }
    }
};

}//namespace cura
#endif//UTILS_OPTIONAL_H

