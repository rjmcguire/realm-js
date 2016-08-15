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

#include "js_class.hpp"
#include "js_types.hpp"
#include "js_observable.hpp"

#include "collection_notifications.hpp"

namespace realm {
namespace js {

// Empty class that merely serves as useful type for now.
class Collection {};

template<typename T>
struct CollectionClass : ClassDefinition<T, Collection, ObservableClass<T>> {
    using ContextType = typename T::Context;
    using ValueType = typename T::Value;
    using ObjectType = typename T::Object;
    using Object = js::Object<T>;
    using Value = js::Value<T>;

    std::string const name = "Collection";
    
    static inline ValueType create_collection_change_set(ContextType ctx, const CollectionChangeSet &change_set);
};

template<typename T>
typename T::Value CollectionClass<T>::create_collection_change_set(ContextType ctx, const CollectionChangeSet &change_set)
{
    ObjectType object = Object::create_empty(ctx);
    std::vector<ValueType> scratch;
    auto create_array = [&](const auto& indices, const char* name) {
        scratch.clear();
        for (auto index : indices) {
            scratch.push_back(Value::from_number(ctx, index));
        }
        Object::set_property(ctx, object, name, Object::create_array(ctx, scratch));
    };

    create_array(change_set.deletions.as_indexes(), "deletions");
    create_array(change_set.insertions.as_indexes(), "insertions");
    create_array(change_set.modifications.as_indexes(), "modifications");

    return object;
}

template<typename T, typename Parent>
class ObservableCollection : public Parent {
public:
    using ContextType = typename T::Context;
    using ObjectType = typename T::Object;
    using ValueType = typename T::Value;
    using FunctionType = typename T::Function;
    using Object = js::Object<T>;
    using Value = js::Value<T>;

    using Parent::Parent;
    using Parent::operator=;
    ObservableCollection(Parent p) : Parent(std::move(p)) { }

    void add_listener(ContextType, ObjectType, ValueType);
    void remove_listener(Protected<ObjectType>);
    void remove_all_listeners();

private:
    std::vector<std::pair<Protected<typename T::Object>, NotificationToken>> m_notification_tokens;
};

template<typename T, typename P>
void ObservableCollection<T, P>::add_listener(ContextType ctx, ObjectType this_object, ValueType value) {
    if (Value::is_function(ctx, value)) {
        auto callback = Value::validated_to_function(ctx, value);
        Protected<FunctionType> protected_callback(ctx, callback);
        Protected<ObjectType> protected_this(ctx, this_object);
        Protected<typename T::GlobalContext> protected_ctx(Context<T>::get_global_context(ctx));

        auto token = this->add_notification_callback([=](const CollectionChangeSet& change_set, std::exception_ptr exception) {
            ValueType arguments[2];
            arguments[0] = static_cast<ObjectType>(protected_this);
            arguments[1] = CollectionClass<T>::create_collection_change_set(protected_ctx, change_set);
            Function<T>::call(protected_ctx, protected_callback, protected_this, 2, arguments);
        });
        m_notification_tokens.emplace_back(Protected<ObjectType>(ctx, callback), std::move(token));
    }
    else {
        struct Callback {
            Protected<ObjectType> protected_this;
            Protected<typename T::GlobalContext> protected_ctx;

            Protected<FunctionType> before_fn, after_fn, error_fn;

            void before(const CollectionChangeSet& changes) {
                ValueType arguments[] = {static_cast<ObjectType>(protected_this), to_array(changes.deletions), to_array(changes.modifications)};
                Function<T>::call(protected_ctx, before_fn, protected_this, 3, arguments);
            }
            void after(const CollectionChangeSet& changes) {
                ValueType arguments[] = {static_cast<ObjectType>(protected_this), to_array(changes.insertions), to_array(changes.modifications_new)};
                Function<T>::call(protected_ctx, after_fn, protected_this, 3, arguments);
            }
            void error(std::exception_ptr exception) {
                try {
                    std::rethrow_exception(exception);
                }
                catch (const std::exception& e) {
                    ValueType arguments[] = {static_cast<ObjectType>(protected_this), Value::from_string(protected_ctx, e.what())};
                    Function<T>::call(protected_ctx, error_fn, protected_this, 2, arguments);
                }
                catch (...) {
                    ValueType arguments[] = {static_cast<ObjectType>(protected_this), Value::from_string(protected_ctx, "unknown error")};
                    Function<T>::call(protected_ctx, error_fn, protected_this, 2, arguments);
                }
            }

            std::vector<ValueType> scratch;
            auto to_array(const IndexSet& index_set) {
                scratch.clear();
                for (auto index : index_set.as_indexes()) {
                    scratch.push_back(Value::from_number(protected_ctx, index));
                }
                return Object::create_array(protected_ctx, scratch);
            }
        } callback;
        callback.protected_ctx = Protected<typename T::GlobalContext>(Context<T>::get_global_context(ctx));
        callback.protected_this = Protected<ObjectType>(ctx, this_object);

        auto obj = Value::validated_to_object(ctx, value);
        auto get_function = [&](Protected<FunctionType>& fn, const char* name) {
            ValueType value = Object::get_property(ctx, obj, name);
            if (!Value::is_undefined(ctx, value)) {
                auto function = Value::validated_to_function(ctx, value, name);
                fn = Protected<FunctionType>(ctx, function);
            }
        };
        get_function(callback.before_fn, "before");
        get_function(callback.after_fn, "after");
        get_function(callback.error_fn, "error");

        auto token = this->add_notification_callback(std::move(callback));
        m_notification_tokens.emplace_back(Protected<ObjectType>(ctx, obj), std::move(token));
    }
}

template<typename T, typename P>
void ObservableCollection<T, P>::remove_listener(Protected<ObjectType> protected_function) {
    typename Protected<ObjectType>::Comparator compare;
    m_notification_tokens.erase(std::remove_if(m_notification_tokens.begin(), m_notification_tokens.end(),
                                               [&](auto&& token) { return compare(token.first, protected_function); }),
                                m_notification_tokens.end());
}

template<typename T, typename P>
void ObservableCollection<T, P>::remove_all_listeners() {
    m_notification_tokens.clear();
}

} // js
} // realm
