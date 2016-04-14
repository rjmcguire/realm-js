////////////////////////////////////////////////////////////////////////////
//
// Copyright 2016 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "jsc_types.hpp"
#include "js_class.hpp"
#include "js_util.hpp"

namespace realm {
namespace jsc {

template<typename T>
using ObjectClass = js::ObjectClass<Types, T>;

using BaseObjectClass = js::BaseObjectClass<Types>;
using ConstructorType = js::ConstructorType<Types>;
using MethodType = js::MethodType<Types>;
using PropertyGetterType = js::PropertyGetterType<Types>;
using PropertySetterType = js::PropertySetterType<Types>;
using IndexPropertyGetterType = js::IndexPropertyGetterType<Types>;
using IndexPropertySetterType = js::IndexPropertySetterType<Types>;
using StringPropertyGetterType = js::StringPropertyGetterType<Types>;
using StringPropertySetterType = js::StringPropertySetterType<Types>;
using StringPropertyEnumeratorType = js::StringPropertyEnumeratorType<Types>;
using MethodMap = js::MethodMap<Types>;
using PropertyMap = js::PropertyMap<Types>;

template<typename T>
class ObjectWrap {
    static ObjectClass<T> s_class;

    std::unique_ptr<T> m_object;

    ObjectWrap(T* object = nullptr) : m_object(object) {}

    static JSObjectRef construct(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef arguments[], JSValueRef* exception) {
        if (!s_class.constructor) {
            *exception = jsc::Exception::value(ctx, "Illegal constructor");
            return nullptr;
        }

        JSObjectRef this_object = ObjectWrap<T>::create(ctx);
        try {
            s_class.constructor(ctx, this_object, argc, arguments);
        }
        catch(std::exception &e) {
            *exception = jsc::Exception::value(ctx, e);
        }
        return this_object;
    }

    static bool has_instance(JSContextRef ctx, JSObjectRef constructor, JSValueRef value, JSValueRef* exception) {
        return JSValueIsObjectOfClass(ctx, value, get_class());
    }

    static JSValueRef get_property(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
        if (auto index_getter = s_class.index_accessor.getter) {
            try {
                uint32_t index = validated_positive_index(jsc::String(property));
                return index_getter(ctx, object, index, exception);
            }
            catch (std::out_of_range &) {
                // Out-of-bounds index getters should just return undefined in JS.
                return Value::from_undefined(ctx);
            }
            catch (std::invalid_argument &) {
                // Property is not a number.
            }
        }
        if (auto string_getter = s_class.string_accessor.getter) {
            return string_getter(ctx, object, property, exception);
        }
        return nullptr;
    }

    static bool set_property(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef value, JSValueRef* exception) {
        auto index_setter = s_class.index_accessor.setter;

        if (index_setter || s_class.index_accessor.getter) {
            try {
                uint32_t index = validated_positive_index(jsc::String(property));

                if (index_setter) {
                    return index_setter(ctx, object, index, value, exception);
                }
                else {
                    *exception = Exception::value(ctx, std::string("Cannot assigned to read only index ") + util::to_string(index));
                    return false;
                }
            }
            catch (std::out_of_range &e) {
                *exception = Exception::value(ctx, e);
                return false;
            }
            catch (std::invalid_argument &) {
                // Property is not a number.
            }
        }
        if (auto string_setter = s_class.string_accessor.setter) {
            return string_setter(ctx, object, property, value, exception);
        }
        return false;
    }

    static bool set_readonly_property(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef value, JSValueRef* exception) {
        *exception = Exception::value(ctx, std::string("Cannot assign to read only property '") + std::string(String(property)) + "'");
        return false;
    }

    static void get_property_names(JSContextRef ctx, JSObjectRef object, JSPropertyNameAccumulatorRef accumulator) {
        if (s_class.index_accessor.getter) {
            try {
                uint32_t length = Object::validated_get_length(ctx, object);
                char string[32];
                for (uint32_t i = 0; i < length; i++) {
                    sprintf(string, "%u", i);
                    JSPropertyNameAccumulatorAddName(accumulator, jsc::String(string));
                }
            }
            catch (std::exception &) {
                // Enumerating properties should never throw an exception into JS.
            }
        }
        if (auto string_enumerator = s_class.string_accessor.enumerator) {
            string_enumerator(ctx, object, accumulator);
        }
    }

    static void finalize(JSObjectRef object) {
        // This is called for the most derived class before superclasses.
        if (auto wrap = static_cast<ObjectWrap<T> *>(JSObjectGetPrivate(object))) {
            delete wrap;
            JSObjectSetPrivate(object, nullptr);
        }
    }

    template<typename U>
    static JSClassRef get_superclass(ObjectClass<U>*) {
        return ObjectWrap<U>::get_class();
    }

    static std::vector<JSStaticFunction> get_methods(const MethodMap &methods) {
        std::vector<JSStaticFunction> functions;
        functions.reserve(methods.size() + 1);

        JSPropertyAttributes attributes = kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontEnum | kJSPropertyAttributeDontDelete;
        size_t index = 0;

        for (auto &pair : methods) {
            functions[index++] = {pair.first.c_str(), pair.second, attributes};
        }

        functions[index] = {0};
        return functions;
    }

    static std::vector<JSStaticValue> get_properties(const PropertyMap &properties) {
        std::vector<JSStaticValue> values;
        values.reserve(properties.size() + 1);

        JSPropertyAttributes attributes = kJSPropertyAttributeDontEnum | kJSPropertyAttributeDontDelete;
        size_t index = 0;

        for (auto &pair : properties) {
            auto &prop = pair.second;
            values[index++] = {pair.first.c_str(), prop.getter, prop.setter ?: set_readonly_property, attributes};
        }

        values[index] = {0};
        return values;
    }

    static JSClassRef create_class() {
        JSClassDefinition definition = kJSClassDefinitionEmpty;
        std::vector<JSStaticFunction> methods;
        std::vector<JSStaticValue> properties;

        definition.parentClass = get_superclass(s_class.superclass);
        definition.className = s_class.name.c_str();
        definition.finalize = finalize;

        if (!s_class.methods.empty()) {
            methods = get_methods(s_class.methods);
            definition.staticFunctions = methods.data();
        }
        if (!s_class.properties.empty()) {
            properties = get_properties(s_class.properties);
            definition.staticValues = properties.data();
        }

        if (s_class.index_accessor.getter || s_class.string_accessor.getter) {
            definition.getProperty = get_property;
            definition.setProperty = set_property;
        }
        else if (s_class.index_accessor.setter || s_class.string_accessor.setter) {
            definition.setProperty = set_property;
        }

        if (s_class.index_accessor.getter || s_class.string_accessor.enumerator) {
            definition.getPropertyNames = get_property_names;
        }

        return JSClassCreate(&definition);
    }

    static JSClassRef create_constructor_class() {
        // Skip creating a special constructor class if possible.
        if (!s_class.constructor && s_class.static_methods.empty() && s_class.static_properties.empty()) {
            return nullptr;
        }

        JSClassDefinition definition = kJSClassDefinitionEmpty;
        std::vector<JSStaticFunction> methods;
        std::vector<JSStaticValue> properties;

        definition.attributes = kJSClassAttributeNoAutomaticPrototype;
        definition.className = s_class.name.c_str();
        definition.hasInstance = has_instance;

        if (s_class.constructor) {
            definition.callAsConstructor = construct;
        }
        if (!s_class.static_methods.empty()) {
            methods = get_methods(s_class.static_methods);
            definition.staticFunctions = methods.data();
        }
        if (!s_class.static_properties.empty()) {
            properties = get_properties(s_class.static_properties);
            definition.staticValues = properties.data();
        }

        return JSClassCreate(&definition);
    }

  public:
    operator T*() const {
        return m_object.get();
    }
    ObjectWrap<T>& operator=(T* object) {
        if (m_object.get() != object) {
            m_object = std::unique_ptr<T>(object);
        }
        return *this;
    }

    static JSClassRef get_class() {
        static JSClassRef js_class = create_class();
        return js_class;
    }

    static JSClassRef get_constructor_class() {
        static JSClassRef js_class = create_constructor_class();
        return js_class;
    }

    static JSObjectRef create(JSContextRef ctx, T* internal = nullptr) {
        return JSObjectMake(ctx, get_class(), new ObjectWrap<T>(internal));
    }

    static JSObjectRef create_constructor(JSContextRef ctx) {
        if (JSClassRef constructor_class = get_constructor_class()) {
            return JSObjectMake(ctx, constructor_class, nullptr);
        }

        return JSObjectMakeConstructor(ctx, get_class(), construct);
    }

    static bool has_instance(JSContextRef ctx, JSValueRef value) {
        return JSValueIsObjectOfClass(ctx, value, get_class());
    }
};

// Make the top-level base class return a NULL JSClassRef.
template<>
inline JSClassRef ObjectWrap<void>::get_class() {
    return nullptr;
}

// The declared static variables must be defined as well.
template<typename T> ObjectClass<T> ObjectWrap<T>::s_class;

} // jsc

namespace js {

template<typename T>
class ObjectWrap<jsc::Types, T> : public jsc::ObjectWrap<T> {};

template<jsc::MethodType F>
JSValueRef wrap(JSContextRef ctx, JSObjectRef function, JSObjectRef this_object, size_t argc, const JSValueRef arguments[], JSValueRef* exception) {
    jsc::ReturnValue return_value(ctx);
    try {
        F(ctx, this_object, argc, arguments, return_value);
    }
    catch(std::exception &e) {
        *exception = jsc::Exception::value(ctx, e);
    }
    return return_value;
}

template<jsc::PropertyGetterType F>
JSValueRef wrap(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    jsc::ReturnValue return_value(ctx);
    try {
        F(ctx, object, return_value);
    }
    catch(std::exception &e) {
        *exception = jsc::Exception::value(ctx, e);
    }
    return return_value;
}

template<jsc::PropertySetterType F>
bool wrap(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef value, JSValueRef* exception) {
    try {
        F(ctx, object, value);
        return true;
    }
    catch(std::exception &e) {
        *exception = jsc::Exception::value(ctx, e);
    }
    return false;
}

template<jsc::IndexPropertyGetterType F>
JSValueRef wrap(JSContextRef ctx, JSObjectRef object, uint32_t index, JSValueRef* exception) {
    jsc::ReturnValue return_value(ctx);
    try {
        F(ctx, object, index, return_value);
    }
    catch (std::out_of_range &) {
        // Out-of-bounds index getters should just return undefined in JS.
        return jsc::Value::from_undefined(ctx);
    }
    catch(std::exception &e) {
        *exception = jsc::Exception::value(ctx, e);
    }
    return return_value;
}

template<jsc::IndexPropertySetterType F>
bool wrap(JSContextRef ctx, JSObjectRef object, uint32_t index, JSValueRef value, JSValueRef* exception) {
    try {
        return F(ctx, object, index, value);
    }
    catch(std::exception &e) {
        *exception = jsc::Exception::value(ctx, e);
    }
    return false;
}

template<jsc::StringPropertyGetterType F>
JSValueRef wrap(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    jsc::ReturnValue return_value(ctx);
    try {
        F(ctx, object, property, return_value);
    }
    catch(std::exception &e) {
        *exception = jsc::Exception::value(ctx, e);
    }
    return return_value;
}

template<jsc::StringPropertySetterType F>
bool wrap(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef value, JSValueRef* exception) {
    try {
        return F(ctx, object, property, value);
    }
    catch(std::exception &e) {
        *exception = jsc::Exception::value(ctx, e);
    }
    return false;
}

template<jsc::StringPropertyEnumeratorType F>
void wrap(JSContextRef ctx, JSObjectRef object, JSPropertyNameAccumulatorRef accumulator) {
    auto names = F(ctx, object);
    for (auto &name : names) {
        JSPropertyNameAccumulatorAddName(accumulator, name);
    }
}

} // js
} // realm
