/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "bluez_backend.hpp"
#include "brookesia/hal_linux/macro_configs.h"

#if BROOKESIA_HAL_LINUX_BLE_BACKEND_BLUEZ_AVAILABLE
#include <gio/gio.h>
#endif

namespace esp_brookesia::hal {

namespace {

void report_error(
    const bluetooth::ble::PeripheralIface::Callbacks &callbacks, const std::string &operation, int code,
    const std::string &message
)
{
    if (callbacks.on_error) {
        callbacks.on_error(operation, code, message);
    }
}

#if BROOKESIA_HAL_LINUX_BLE_BACKEND_BLUEZ_AVAILABLE
bluetooth::ble::CharacteristicId normalized_characteristic(const bluetooth::ble::CharacteristicId &characteristic)
{
    return {
        .service_uuid = bluetooth::ble::normalize_uuid(characteristic.service_uuid),
        .characteristic_uuid = bluetooth::ble::normalize_uuid(characteristic.characteristic_uuid),
    };
}
#endif

} // namespace

class BluezPeripheralBackend::Impl {
public:
    mutable std::mutex mutex;
    bluetooth::ble::PeripheralConfig config;
    bluetooth::ble::PeripheralIface::Callbacks callbacks;
    bool configured = false;
    bool initialized = false;
    bool started = false;
    bool advertising = false;
    bool shutting_down = false;

#if BROOKESIA_HAL_LINUX_BLE_BACKEND_BLUEZ_AVAILABLE
    struct ServiceRecord {
        std::string path;
        std::string uuid;
        guint registration_id = 0;
    };

    struct CharacteristicRecord {
        std::string path;
        std::string service_path;
        bluetooth::ble::CharacteristicId id;
        bluetooth::ble::CharacteristicConfig config;
        bluetooth::ble::ByteArray value;
        bool notifying = false;
        guint registration_id = 0;
    };

    GMainContext *context = nullptr;
    GMainLoop *loop = nullptr;
    GDBusConnection *connection = nullptr;
    GDBusNodeInfo *object_manager_node = nullptr;
    GDBusNodeInfo *service_node = nullptr;
    GDBusNodeInfo *characteristic_node = nullptr;
    GDBusNodeInfo *advertisement_node = nullptr;
    guint root_registration_id = 0;
    guint advertisement_registration_id = 0;
    guint properties_subscription_id = 0;
    std::string adapter_path;
    std::vector<ServiceRecord> services;
    std::vector<CharacteristicRecord> characteristics;
    std::map<uint16_t, bluetooth::ble::ConnectionInfo> connections;
    std::map<uint16_t, std::string> device_paths;
    std::thread loop_thread;
    std::mutex advertisement_call_mutex;
    std::mutex manager_threads_mutex;
    std::vector<std::thread> manager_threads;
    bool accept_manager_operations = true;
    uint16_t next_connection_id = 1;
    bool application_registered = false;
    bool advertisement_registered = false;

    static constexpr const char *BLUEZ_BUS_NAME = "org.bluez";
    static constexpr const char *APP_ROOT_PATH = "/com/espressif/BrookesiaBle";
    static constexpr const char *ADVERTISEMENT_PATH = "/com/espressif/BrookesiaBle/advertisement0";

    static constexpr const char *OBJECT_MANAGER_XML = R"xml(
        <node>
          <interface name='org.freedesktop.DBus.ObjectManager'>
            <method name='GetManagedObjects'>
              <arg name='objects' type='a{oa{sa{sv}}}' direction='out'/>
            </method>
          </interface>
        </node>
    )xml";
    static constexpr const char *SERVICE_XML = R"xml(
        <node>
          <interface name='org.bluez.GattService1'>
            <property name='UUID' type='s' access='read'/>
            <property name='Primary' type='b' access='read'/>
            <property name='Includes' type='ao' access='read'/>
          </interface>
        </node>
    )xml";
    static constexpr const char *CHARACTERISTIC_XML = R"xml(
        <node>
          <interface name='org.bluez.GattCharacteristic1'>
            <method name='WriteValue'>
              <arg name='value' type='ay' direction='in'/>
              <arg name='options' type='a{sv}' direction='in'/>
            </method>
            <method name='StartNotify'/>
            <method name='StopNotify'/>
            <property name='UUID' type='s' access='read'/>
            <property name='Service' type='o' access='read'/>
            <property name='Flags' type='as' access='read'/>
            <property name='Notifying' type='b' access='read'/>
            <property name='Value' type='ay' access='read'/>
          </interface>
        </node>
    )xml";
    static constexpr const char *ADVERTISEMENT_XML = R"xml(
        <node>
          <interface name='org.bluez.LEAdvertisement1'>
            <method name='Release'/>
            <property name='Type' type='s' access='read'/>
            <property name='ServiceUUIDs' type='as' access='read'/>
            <property name='LocalName' type='s' access='read'/>
          </interface>
        </node>
    )xml";

    static const GDBusInterfaceVTable OBJECT_MANAGER_VTABLE;
    static const GDBusInterfaceVTable SERVICE_VTABLE;
    static const GDBusInterfaceVTable CHARACTERISTIC_VTABLE;
    static const GDBusInterfaceVTable ADVERTISEMENT_VTABLE;

    bool initialize(std::string *error_message)
    {
        {
            std::lock_guard lock(mutex);
            shutting_down = false;
        }
        {
            std::lock_guard lock(manager_threads_mutex);
            accept_manager_operations = true;
        }
        GError *error = nullptr;
        context = g_main_context_new();
        g_main_context_push_thread_default(context);
        connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
        if (connection == nullptr) {
            assign_error(error_message, "Cannot connect to the system D-Bus", error);
            g_clear_error(&error);
            g_main_context_pop_thread_default(context);
            return false;
        }

        if (!find_adapter(error_message)) {
            g_main_context_pop_thread_default(context);
            return false;
        }
        if (!create_introspection(error_message)) {
            g_main_context_pop_thread_default(context);
            return false;
        }
        if (!register_objects(error_message)) {
            g_main_context_pop_thread_default(context);
            return false;
        }

        properties_subscription_id = g_dbus_connection_signal_subscribe(
                                         connection, BLUEZ_BUS_NAME, "org.freedesktop.DBus.Properties",
                                         "PropertiesChanged", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
                                         on_properties_changed, this, nullptr
                                     );
        loop = g_main_loop_new(context, FALSE);
        g_main_context_pop_thread_default(context);
        loop_thread = std::thread([this]() {
            g_main_context_push_thread_default(context);
            g_main_loop_run(loop);
            g_main_context_pop_thread_default(context);
        });

        GVariant *reply = g_dbus_connection_call_sync(
                              connection, BLUEZ_BUS_NAME, adapter_path.c_str(), "org.bluez.GattManager1",
                              "RegisterApplication",
                              g_variant_new("(o@a{sv})", APP_ROOT_PATH, make_empty_options()), nullptr,
                              G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &error
                          );
        if (reply == nullptr) {
            assign_error(error_message, "BlueZ RegisterApplication failed", error);
            g_clear_error(&error);
            shutdown();
            return false;
        }
        g_variant_unref(reply);
        application_registered = true;
        return true;
    }

    void shutdown()
    {
        {
            std::lock_guard lock(mutex);
            shutting_down = true;
            started = false;
        }

        std::vector<std::thread> pending_operations;
        {
            std::lock_guard lock(manager_threads_mutex);
            accept_manager_operations = false;
            pending_operations.swap(manager_threads);
        }
        for (auto &operation : pending_operations) {
            if (operation.joinable()) {
                operation.join();
            }
        }

        if (connection != nullptr) {
            (void)unregister_advertisement();
        }
        if (connection != nullptr && application_registered) {
            call_manager_sync("org.bluez.GattManager1", "UnregisterApplication", APP_ROOT_PATH);
            application_registered = false;
        }
        if (connection != nullptr && properties_subscription_id != 0) {
            g_dbus_connection_signal_unsubscribe(connection, properties_subscription_id);
            properties_subscription_id = 0;
        }
        if (connection != nullptr) {
            for (auto &characteristic : characteristics) {
                if (characteristic.registration_id != 0) {
                    g_dbus_connection_unregister_object(connection, characteristic.registration_id);
                    characteristic.registration_id = 0;
                }
            }
            for (auto &service : services) {
                if (service.registration_id != 0) {
                    g_dbus_connection_unregister_object(connection, service.registration_id);
                    service.registration_id = 0;
                }
            }
            if (advertisement_registration_id != 0) {
                g_dbus_connection_unregister_object(connection, advertisement_registration_id);
                advertisement_registration_id = 0;
            }
            if (root_registration_id != 0) {
                g_dbus_connection_unregister_object(connection, root_registration_id);
                root_registration_id = 0;
            }
        }
        if (loop != nullptr) {
            g_main_loop_quit(loop);
        }
        if (loop_thread.joinable()) {
            loop_thread.join();
        }
        if (loop != nullptr) {
            g_main_loop_unref(loop);
            loop = nullptr;
        }
        g_clear_pointer(&object_manager_node, g_dbus_node_info_unref);
        g_clear_pointer(&service_node, g_dbus_node_info_unref);
        g_clear_pointer(&characteristic_node, g_dbus_node_info_unref);
        g_clear_pointer(&advertisement_node, g_dbus_node_info_unref);
        g_clear_object(&connection);
        if (context != nullptr) {
            g_main_context_unref(context);
            context = nullptr;
        }
        services.clear();
        characteristics.clear();
        connections.clear();
        device_paths.clear();
    }

    bool register_advertisement(std::string *error_message = nullptr)
    {
        std::lock_guard operation_lock(advertisement_call_mutex);
        if (advertisement_registered) {
            return true;
        }
        GError *error = nullptr;
        GVariant *reply = g_dbus_connection_call_sync(
                              connection, BLUEZ_BUS_NAME, adapter_path.c_str(),
                              "org.bluez.LEAdvertisingManager1", "RegisterAdvertisement",
                              g_variant_new("(o@a{sv})", ADVERTISEMENT_PATH, make_empty_options()), nullptr,
                              G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &error
                          );
        if (reply == nullptr) {
            assign_error(error_message, "BlueZ RegisterAdvertisement failed", error);
            g_clear_error(&error);
            return false;
        }
        g_variant_unref(reply);
        advertisement_registered = true;
        return true;
    }

    bool unregister_advertisement(std::string *error_message = nullptr)
    {
        std::lock_guard operation_lock(advertisement_call_mutex);
        if (!advertisement_registered) {
            return true;
        }
        GError *error = nullptr;
        GVariant *reply = g_dbus_connection_call_sync(
                              connection, BLUEZ_BUS_NAME, adapter_path.c_str(),
                              "org.bluez.LEAdvertisingManager1", "UnregisterAdvertisement",
                              g_variant_new("(o)", ADVERTISEMENT_PATH), nullptr, G_DBUS_CALL_FLAGS_NONE,
                              10000, nullptr, &error
                          );
        if (reply == nullptr) {
            assign_error(error_message, "BlueZ UnregisterAdvertisement failed", error);
            g_clear_error(&error);
            return false;
        }
        g_variant_unref(reply);
        advertisement_registered = false;
        return true;
    }

    bool queue_manager_operation(std::function<void()> operation)
    {
        std::lock_guard lock(manager_threads_mutex);
        if (!accept_manager_operations) {
            return false;
        }
        manager_threads.emplace_back([operation = std::move(operation)]() mutable {
            operation();
        });
        return true;
    }

    CharacteristicRecord *find_characteristic(const bluetooth::ble::CharacteristicId &id)
    {
        const auto normalized = normalized_characteristic(id);
        const auto result = std::find_if(
                                characteristics.begin(), characteristics.end(),
        [&normalized](const auto & item) {
            return item.id == normalized;
        }
                            );
        return result == characteristics.end() ? nullptr : &*result;
    }

    const CharacteristicRecord *find_characteristic(const bluetooth::ble::CharacteristicId &id) const
    {
        const auto normalized = normalized_characteristic(id);
        const auto result = std::find_if(
                                characteristics.begin(), characteristics.end(),
        [&normalized](const auto & item) {
            return item.id == normalized;
        }
                            );
        return result == characteristics.end() ? nullptr : &*result;
    }

private:
    static GVariant *make_empty_options()
    {
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
        return g_variant_builder_end(&options);
    }

    static void assign_error(std::string *target, const char *prefix, const GError *error)
    {
        if (target != nullptr) {
            *target = prefix;
            if (error != nullptr && error->message != nullptr) {
                target->append(": ").append(error->message);
            }
        }
    }

    bool find_adapter(std::string *error_message)
    {
        GError *error = nullptr;
        GVariant *reply = g_dbus_connection_call_sync(
                              connection, BLUEZ_BUS_NAME, "/", "org.freedesktop.DBus.ObjectManager",
                              "GetManagedObjects", nullptr, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
                              G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &error
                          );
        if (reply == nullptr) {
            assign_error(error_message, "Cannot query BlueZ adapters", error);
            g_clear_error(&error);
            return false;
        }

        GVariantIter *objects = nullptr;
        g_variant_get(reply, "(a{oa{sa{sv}}})", &objects);
        const char *path = nullptr;
        GVariant *interfaces = nullptr;
        while (g_variant_iter_next(objects, "{&o@a{sa{sv}}}", &path, &interfaces)) {
            GVariant *gatt = g_variant_lookup_value(interfaces, "org.bluez.GattManager1", nullptr);
            GVariant *advertising = g_variant_lookup_value(interfaces, "org.bluez.LEAdvertisingManager1", nullptr);
            if ((gatt != nullptr) && (advertising != nullptr)) {
                adapter_path = path;
            }
            g_clear_pointer(&gatt, g_variant_unref);
            g_clear_pointer(&advertising, g_variant_unref);
            g_variant_unref(interfaces);
            if (!adapter_path.empty()) {
                break;
            }
        }
        g_variant_iter_free(objects);
        g_variant_unref(reply);
        if (adapter_path.empty()) {
            if (error_message != nullptr) {
                *error_message = "No BlueZ adapter exposes both GattManager1 and LEAdvertisingManager1";
            }
            return false;
        }
        return true;
    }

    bool create_introspection(std::string *error_message)
    {
        GError *error = nullptr;
        object_manager_node = g_dbus_node_info_new_for_xml(OBJECT_MANAGER_XML, &error);
        service_node = g_dbus_node_info_new_for_xml(SERVICE_XML, &error);
        characteristic_node = g_dbus_node_info_new_for_xml(CHARACTERISTIC_XML, &error);
        advertisement_node = g_dbus_node_info_new_for_xml(ADVERTISEMENT_XML, &error);
        if ((object_manager_node == nullptr) || (service_node == nullptr) ||
                (characteristic_node == nullptr) || (advertisement_node == nullptr)) {
            assign_error(error_message, "Cannot create BlueZ D-Bus introspection data", error);
            g_clear_error(&error);
            return false;
        }
        return true;
    }

    bool register_objects(std::string *error_message)
    {
        GError *error = nullptr;
        root_registration_id = g_dbus_connection_register_object(
                                   connection, APP_ROOT_PATH, object_manager_node->interfaces[0],
                                   &OBJECT_MANAGER_VTABLE, this, nullptr, &error
                               );
        if (root_registration_id == 0) {
            assign_error(error_message, "Cannot export BlueZ object manager", error);
            g_clear_error(&error);
            return false;
        }

        for (size_t service_index = 0; service_index < config.services.size(); ++service_index) {
            const auto &service_config = config.services[service_index];
            ServiceRecord service{
                .path = std::string(APP_ROOT_PATH) + "/service" + std::to_string(service_index),
                .uuid = service_config.uuid,
            };
            service.registration_id = g_dbus_connection_register_object(
                                          connection, service.path.c_str(), service_node->interfaces[0],
                                          &SERVICE_VTABLE, this, nullptr, &error
                                      );
            if (service.registration_id == 0) {
                assign_error(error_message, "Cannot export BlueZ GATT service", error);
                g_clear_error(&error);
                return false;
            }
            services.push_back(service);

            for (size_t characteristic_index = 0;
                    characteristic_index < service_config.characteristics.size(); ++characteristic_index) {
                const auto &characteristic_config = service_config.characteristics[characteristic_index];
                CharacteristicRecord characteristic{
                    .path = service.path + "/char" + std::to_string(characteristic_index),
                    .service_path = service.path,
                    .id = {service.uuid, characteristic_config.uuid},
                    .config = characteristic_config,
                    .value = {},
                };
                characteristic.registration_id = g_dbus_connection_register_object(
                                                     connection, characteristic.path.c_str(),
                                                     characteristic_node->interfaces[0], &CHARACTERISTIC_VTABLE,
                                                     this, nullptr, &error
                                                 );
                if (characteristic.registration_id == 0) {
                    assign_error(error_message, "Cannot export BlueZ GATT characteristic", error);
                    g_clear_error(&error);
                    return false;
                }
                characteristics.push_back(std::move(characteristic));
            }
        }

        advertisement_registration_id = g_dbus_connection_register_object(
                                            connection, ADVERTISEMENT_PATH, advertisement_node->interfaces[0],
                                            &ADVERTISEMENT_VTABLE, this, nullptr, &error
                                        );
        if (advertisement_registration_id == 0) {
            assign_error(error_message, "Cannot export BlueZ LE advertisement", error);
            g_clear_error(&error);
            return false;
        }
        return true;
    }

    void call_manager_sync(const char *interface_name, const char *method_name, const char *object_path)
    {
        GError *error = nullptr;
        GVariant *reply = g_dbus_connection_call_sync(
                              connection, BLUEZ_BUS_NAME, adapter_path.c_str(), interface_name, method_name,
                              g_variant_new("(o)", object_path), nullptr, G_DBUS_CALL_FLAGS_NONE, 5000,
                              nullptr, &error
                          );
        g_clear_pointer(&reply, g_variant_unref);
        g_clear_error(&error);
    }

    static ServiceRecord *service_for_path(Impl *self, const char *object_path)
    {
        const auto result = std::find_if(self->services.begin(), self->services.end(), [object_path](const auto & item) {
            return item.path == object_path;
        });
        return result == self->services.end() ? nullptr : &*result;
    }

    static CharacteristicRecord *characteristic_for_path(Impl *self, const char *object_path)
    {
        const auto result = std::find_if(
                                self->characteristics.begin(), self->characteristics.end(),
        [object_path](const auto & item) {
            return item.path == object_path;
        }
                            );
        return result == self->characteristics.end() ? nullptr : &*result;
    }

    static GVariant *make_flags(const CharacteristicRecord &characteristic)
    {
        GVariantBuilder flags;
        g_variant_builder_init(&flags, G_VARIANT_TYPE("as"));
        if (characteristic.config.write) {
            g_variant_builder_add(&flags, "s", "write");
        }
        if (characteristic.config.write_without_response) {
            g_variant_builder_add(&flags, "s", "write-without-response");
        }
        if (characteristic.config.notify) {
            g_variant_builder_add(&flags, "s", "notify");
        }
        return g_variant_builder_end(&flags);
    }

    static GVariant *make_byte_array(const bluetooth::ble::ByteArray &data)
    {
        GVariantBuilder value;
        g_variant_builder_init(&value, G_VARIANT_TYPE("ay"));
        for (const auto byte : data) {
            g_variant_builder_add(&value, "y", byte);
        }
        return g_variant_builder_end(&value);
    }

    static GVariant *make_service_properties(const ServiceRecord &service)
    {
        GVariantBuilder properties;
        g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&properties, "{sv}", "UUID", g_variant_new_string(service.uuid.c_str()));
        g_variant_builder_add(&properties, "{sv}", "Primary", g_variant_new_boolean(TRUE));
        GVariantBuilder includes;
        g_variant_builder_init(&includes, G_VARIANT_TYPE("ao"));
        g_variant_builder_add(&properties, "{sv}", "Includes", g_variant_builder_end(&includes));
        return g_variant_builder_end(&properties);
    }

    static GVariant *make_characteristic_properties(const CharacteristicRecord &characteristic)
    {
        GVariantBuilder properties;
        g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(
            &properties, "{sv}", "UUID", g_variant_new_string(characteristic.id.characteristic_uuid.c_str())
        );
        g_variant_builder_add(
            &properties, "{sv}", "Service", g_variant_new_object_path(characteristic.service_path.c_str())
        );
        g_variant_builder_add(&properties, "{sv}", "Flags", make_flags(characteristic));
        g_variant_builder_add(
            &properties, "{sv}", "Notifying", g_variant_new_boolean(characteristic.notifying)
        );
        g_variant_builder_add(&properties, "{sv}", "Value", make_byte_array(characteristic.value));
        return g_variant_builder_end(&properties);
    }

    static void add_managed_interface(
        GVariantBuilder *objects, const std::string &path, const char *interface_name, GVariant *properties
    )
    {
        GVariantBuilder interfaces;
        g_variant_builder_init(&interfaces, G_VARIANT_TYPE("a{sa{sv}}"));
        g_variant_builder_add(&interfaces, "{s@a{sv}}", interface_name, properties);
        g_variant_builder_add(objects, "{o@a{sa{sv}}}", path.c_str(), g_variant_builder_end(&interfaces));
    }

    static void on_object_manager_method_call(
        GDBusConnection *, const char *, const char *, const char *, const char *method_name,
        GVariant *, GDBusMethodInvocation *invocation, gpointer user_data
    )
    {
        auto *self = static_cast<Impl *>(user_data);
        if (std::string_view(method_name) != "GetManagedObjects") {
            g_dbus_method_invocation_return_dbus_error(
                invocation, "org.freedesktop.DBus.Error.UnknownMethod", "Unknown ObjectManager method"
            );
            return;
        }

        GVariantBuilder objects;
        g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
        std::lock_guard lock(self->mutex);
        for (const auto &service : self->services) {
            add_managed_interface(
                &objects, service.path, "org.bluez.GattService1", make_service_properties(service)
            );
        }
        for (const auto &characteristic : self->characteristics) {
            add_managed_interface(
                &objects, characteristic.path, "org.bluez.GattCharacteristic1",
                make_characteristic_properties(characteristic)
            );
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{oa{sa{sv}}})", &objects));
    }

    static GVariant *on_get_property(
        GDBusConnection *, const char *, const char *object_path, const char *interface_name,
        const char *property_name, GError **, gpointer user_data
    )
    {
        auto *self = static_cast<Impl *>(user_data);
        std::lock_guard lock(self->mutex);
        const std::string_view interface(interface_name);
        const std::string_view property(property_name);
        if (interface == "org.bluez.GattService1") {
            const auto *service = service_for_path(self, object_path);
            if (service == nullptr) {
                return nullptr;
            }
            if (property == "UUID") {
                return g_variant_new_string(service->uuid.c_str());
            }
            if (property == "Primary") {
                return g_variant_new_boolean(TRUE);
            }
            if (property == "Includes") {
                GVariantBuilder includes;
                g_variant_builder_init(&includes, G_VARIANT_TYPE("ao"));
                return g_variant_builder_end(&includes);
            }
        } else if (interface == "org.bluez.GattCharacteristic1") {
            const auto *characteristic = characteristic_for_path(self, object_path);
            if (characteristic == nullptr) {
                return nullptr;
            }
            if (property == "UUID") {
                return g_variant_new_string(characteristic->id.characteristic_uuid.c_str());
            }
            if (property == "Service") {
                return g_variant_new_object_path(characteristic->service_path.c_str());
            }
            if (property == "Flags") {
                return make_flags(*characteristic);
            }
            if (property == "Notifying") {
                return g_variant_new_boolean(characteristic->notifying);
            }
            if (property == "Value") {
                return make_byte_array(characteristic->value);
            }
        } else if (interface == "org.bluez.LEAdvertisement1") {
            if (property == "Type") {
                return g_variant_new_string("peripheral");
            }
            if (property == "LocalName") {
                return g_variant_new_string(self->config.device_name.c_str());
            }
            if (property == "ServiceUUIDs") {
                GVariantBuilder uuids;
                g_variant_builder_init(&uuids, G_VARIANT_TYPE("as"));
                for (const auto &uuid : self->config.advertised_service_uuids) {
                    g_variant_builder_add(&uuids, "s", uuid.c_str());
                }
                return g_variant_builder_end(&uuids);
            }
        }
        return nullptr;
    }

    static void on_characteristic_method_call(
        GDBusConnection *, const char *, const char *object_path, const char *, const char *method_name,
        GVariant *parameters, GDBusMethodInvocation *invocation, gpointer user_data
    )
    {
        auto *self = static_cast<Impl *>(user_data);
        const std::string method(method_name);
        if (method == "WriteValue") {
            self->handle_write(object_path, parameters, invocation);
        } else if (method == "StartNotify") {
            self->handle_subscription(object_path, true, invocation);
        } else if (method == "StopNotify") {
            self->handle_subscription(object_path, false, invocation);
        } else {
            g_dbus_method_invocation_return_dbus_error(
                invocation, "org.freedesktop.DBus.Error.UnknownMethod", "Unknown characteristic method"
            );
        }
    }

    static void on_advertisement_method_call(
        GDBusConnection *, const char *, const char *, const char *, const char *method_name,
        GVariant *, GDBusMethodInvocation *invocation, gpointer user_data
    )
    {
        auto *self = static_cast<Impl *>(user_data);
        if (std::string_view(method_name) == "Release") {
            bluetooth::ble::PeripheralIface::Callbacks callbacks;
            bool changed = false;
            {
                std::lock_guard operation_lock(self->advertisement_call_mutex);
                self->advertisement_registered = false;
            }
            {
                std::lock_guard lock(self->mutex);
                changed = self->advertising;
                self->advertising = false;
                callbacks = self->callbacks;
            }
            if (changed && callbacks.on_advertising_state_changed) {
                callbacks.on_advertising_state_changed(false);
            }
            g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.freedesktop.DBus.Error.UnknownMethod", "Unknown advertisement method"
        );
    }

    static void on_properties_changed(
        GDBusConnection *, const char *, const char *object_path, const char *, const char *,
        GVariant *parameters, gpointer user_data
    )
    {
        auto *self = static_cast<Impl *>(user_data);
        const char *interface_name = nullptr;
        GVariant *changed = nullptr;
        GVariant *invalidated = nullptr;
        g_variant_get(parameters, "(&s@a{sv}@as)", &interface_name, &changed, &invalidated);
        if (std::string_view(interface_name) == "org.bluez.Device1") {
            gboolean connected = FALSE;
            if (g_variant_lookup(changed, "Connected", "b", &connected)) {
                self->handle_device_connection(object_path, connected != FALSE, changed);
            }
        }
        g_variant_unref(changed);
        g_variant_unref(invalidated);
    }

    void handle_device_connection(const char *device_path, bool connected, GVariant *changed)
    {
        bluetooth::ble::PeripheralIface::Callbacks local_callbacks;
        bluetooth::ble::ConnectionInfo connection_info;
        std::vector<bluetooth::ble::CharacteristicId> unsubscribed_characteristics;
        bool emit_connected = false;
        bool emit_disconnected = false;
        bool stop_advertising_after_connect = false;
        bool restart = false;
        {
            std::lock_guard lock(mutex);
            local_callbacks = callbacks;
            if (connected) {
                if (shutting_down || !started || !advertising || !connections.empty()) {
                    return;
                }
                const char *address = nullptr;
                g_variant_lookup(changed, "Address", "&s", &address);
                connection_info = {
                    .connection_id = next_connection_id++,
                    .peer_address = address != nullptr ? address : device_path,
                    .mtu = bluetooth::ble::ATT_MTU_MIN,
                };
                connections.emplace(connection_info.connection_id, connection_info);
                device_paths.emplace(connection_info.connection_id, device_path);
                emit_connected = true;
                stop_advertising_after_connect = advertising;
            } else {
                const auto path = std::find_if(
                                      device_paths.begin(), device_paths.end(),
                [device_path](const auto & item) {
                    return item.second == device_path;
                }
                                  );
                if (path == device_paths.end()) {
                    return;
                }
                const auto info = connections.find(path->first);
                if (info != connections.end()) {
                    connection_info = info->second;
                    connections.erase(info);
                }
                for (auto &characteristic : characteristics) {
                    if (characteristic.notifying) {
                        characteristic.notifying = false;
                        unsubscribed_characteristics.push_back(characteristic.id);
                    }
                }
                device_paths.erase(path);
                emit_disconnected = true;
                restart = started && config.auto_restart_advertising;
            }
        }
        if (emit_connected) {
            if (local_callbacks.on_connection_state_changed) {
                local_callbacks.on_connection_state_changed(connection_info, true, "");
            }
            if (stop_advertising_after_connect) {
                (void)queue_manager_operation([this]() {
                    std::string error_message;
                    if (!unregister_advertisement(&error_message)) {
                        bluetooth::ble::PeripheralIface::Callbacks callbacks;
                        bool report = false;
                        {
                            std::lock_guard lock(mutex);
                            callbacks = this->callbacks;
                            report = !shutting_down;
                        }
                        if (report) {
                            report_error(callbacks, "stop_advertising", -1, error_message);
                        }
                        return;
                    }

                    bluetooth::ble::PeripheralIface::Callbacks callbacks;
                    bool changed = false;
                    {
                        std::lock_guard lock(mutex);
                        if (!shutting_down) {
                            changed = advertising;
                            advertising = false;
                            callbacks = this->callbacks;
                        }
                    }
                    if (changed && callbacks.on_advertising_state_changed) {
                        callbacks.on_advertising_state_changed(false);
                    }
                });
            }
        } else if (emit_disconnected) {
            if (local_callbacks.on_subscription_changed) {
                for (const auto &characteristic : unsubscribed_characteristics) {
                    local_callbacks.on_subscription_changed(connection_info.connection_id, characteristic, false);
                }
            }
            if (local_callbacks.on_connection_state_changed) {
                local_callbacks.on_connection_state_changed(connection_info, false, "remote_disconnect");
            }
            if (restart) {
                (void)queue_manager_operation([this]() {
                    std::string error_message;
                    if (!register_advertisement(&error_message)) {
                        bluetooth::ble::PeripheralIface::Callbacks callbacks;
                        bool report = false;
                        {
                            std::lock_guard lock(mutex);
                            callbacks = this->callbacks;
                            report = !shutting_down;
                        }
                        if (report) {
                            report_error(callbacks, "start_advertising", -1, error_message);
                        }
                        return;
                    }

                    bluetooth::ble::PeripheralIface::Callbacks callbacks;
                    bool keep_registered = false;
                    bool changed = false;
                    {
                        std::lock_guard lock(mutex);
                        keep_registered = !shutting_down && started && connections.empty() &&
                                          config.auto_restart_advertising;
                        if (keep_registered) {
                            changed = !advertising;
                            advertising = true;
                            callbacks = this->callbacks;
                        }
                    }
                    if (!keep_registered) {
                        (void)unregister_advertisement();
                    } else if (changed && callbacks.on_advertising_state_changed) {
                        callbacks.on_advertising_state_changed(true);
                    }
                });
            }
        }
    }

    void handle_write(const char *object_path, GVariant *parameters, GDBusMethodInvocation *invocation)
    {
        enum class WriteError {
            None,
            CharacteristicNotFound,
            ConnectionNotFound,
            NotPermitted,
            InvalidOffset,
        };

        GVariant *value = nullptr;
        GVariant *options = nullptr;
        g_variant_get(parameters, "(@ay@a{sv})", &value, &options);
        gsize length = 0;
        const auto *bytes = static_cast<const uint8_t *>(g_variant_get_fixed_array(value, &length, sizeof(uint8_t)));
        const char *device_path = nullptr;
        guint16 mtu = bluetooth::ble::ATT_MTU_MIN;
        guint16 offset = 0;
        g_variant_lookup(options, "device", "&o", &device_path);
        g_variant_lookup(options, "mtu", "q", &mtu);
        g_variant_lookup(options, "offset", "q", &offset);

        bluetooth::ble::PeripheralIface::Callbacks local_callbacks;
        bluetooth::ble::WriteEvent event;
        uint16_t connection_id = 0;
        uint16_t negotiated_mtu = bluetooth::ble::ATT_MTU_MIN;
        WriteError write_error = WriteError::None;
        bool mtu_changed = false;
        {
            std::lock_guard lock(mutex);
            auto *characteristic = characteristic_for_path(this, object_path);
            const auto path = std::find_if(device_paths.begin(), device_paths.end(), [device_path](const auto & item) {
                return (device_path != nullptr) && (item.second == device_path);
            });
            if (characteristic == nullptr) {
                write_error = WriteError::CharacteristicNotFound;
            } else if ((path == device_paths.end()) || !connections.contains(path->first)) {
                write_error = WriteError::ConnectionNotFound;
            } else if (!characteristic->config.write && !characteristic->config.write_without_response) {
                write_error = WriteError::NotPermitted;
            } else if (offset != 0) {
                write_error = WriteError::InvalidOffset;
            } else {
                connection_id = path->first;
                auto &connection_info = connections.at(connection_id);
                negotiated_mtu = std::clamp<uint16_t>(mtu, bluetooth::ble::ATT_MTU_MIN, bluetooth::ble::ATT_MTU_MAX);
                mtu_changed = connection_info.mtu != negotiated_mtu;
                connection_info.mtu = negotiated_mtu;
                // BlueZ validates and assembles ATT long writes before calling WriteValue, so the complete
                // attribute value can legitimately exceed one ATT PDU's MTU-3 payload.
                bluetooth::ble::ByteArray data;
                if (length > 0) {
                    data.assign(bytes, bytes + length);
                }
                event = {
                    .connection_id = connection_id,
                    .characteristic = characteristic->id,
                    .data = std::move(data),
                };
            }
            local_callbacks = callbacks;
        }
        g_variant_unref(value);
        g_variant_unref(options);
        if (write_error != WriteError::None) {
            const char *error_name = "org.bluez.Error.Failed";
            const char *error_message = "Invalid characteristic write";
            switch (write_error) {
            case WriteError::CharacteristicNotFound:
                error_message = "Characteristic does not exist";
                break;
            case WriteError::ConnectionNotFound:
                error_message = "No tracked BLE connection for write";
                break;
            case WriteError::NotPermitted:
                error_name = "org.bluez.Error.NotPermitted";
                error_message = "Characteristic is not writable";
                break;
            case WriteError::InvalidOffset:
                error_name = "org.bluez.Error.InvalidOffset";
                error_message = "Unsupported characteristic write offset";
                break;
            case WriteError::None:
                break;
            }
            g_dbus_method_invocation_return_dbus_error(invocation, error_name, error_message);
            return;
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
        if (mtu_changed && local_callbacks.on_mtu_changed) {
            local_callbacks.on_mtu_changed(connection_id, negotiated_mtu);
        }
        if (local_callbacks.on_characteristic_written) {
            local_callbacks.on_characteristic_written(event);
        }
    }

    void handle_subscription(const char *object_path, bool subscribed, GDBusMethodInvocation *invocation)
    {
        bluetooth::ble::PeripheralIface::Callbacks local_callbacks;
        bluetooth::ble::CharacteristicId id;
        uint16_t connection_id = 0;
        bool valid = false;
        {
            std::lock_guard lock(mutex);
            auto *characteristic = characteristic_for_path(this, object_path);
            valid = (characteristic != nullptr) && characteristic->config.notify && !connections.empty();
            if (valid) {
                characteristic->notifying = subscribed;
                id = characteristic->id;
                connection_id = connections.begin()->first;
            }
            local_callbacks = callbacks;
        }
        if (!valid) {
            g_dbus_method_invocation_return_dbus_error(
                invocation, "org.bluez.Error.NotConnected", "No active BLE connection"
            );
            return;
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
        if (local_callbacks.on_subscription_changed) {
            local_callbacks.on_subscription_changed(connection_id, id, subscribed);
        }
    }
#endif
};

#if BROOKESIA_HAL_LINUX_BLE_BACKEND_BLUEZ_AVAILABLE
const GDBusInterfaceVTable BluezPeripheralBackend::Impl::OBJECT_MANAGER_VTABLE = {
    .method_call = BluezPeripheralBackend::Impl::on_object_manager_method_call,
    .get_property = nullptr,
    .set_property = nullptr,
    .padding = {},
};
const GDBusInterfaceVTable BluezPeripheralBackend::Impl::SERVICE_VTABLE = {
    .method_call = nullptr,
    .get_property = BluezPeripheralBackend::Impl::on_get_property,
    .set_property = nullptr,
    .padding = {},
};
const GDBusInterfaceVTable BluezPeripheralBackend::Impl::CHARACTERISTIC_VTABLE = {
    .method_call = BluezPeripheralBackend::Impl::on_characteristic_method_call,
    .get_property = BluezPeripheralBackend::Impl::on_get_property,
    .set_property = nullptr,
    .padding = {},
};
const GDBusInterfaceVTable BluezPeripheralBackend::Impl::ADVERTISEMENT_VTABLE = {
    .method_call = BluezPeripheralBackend::Impl::on_advertisement_method_call,
    .get_property = BluezPeripheralBackend::Impl::on_get_property,
    .set_property = nullptr,
    .padding = {},
};
#endif

BluezPeripheralBackend::BluezPeripheralBackend()
    : impl_(std::make_unique<Impl>())
{
}

BluezPeripheralBackend::~BluezPeripheralBackend()
{
    deinit();
}

bool BluezPeripheralBackend::configure(
    const bluetooth::ble::PeripheralConfig &config, bluetooth::ble::PeripheralIface::Callbacks callbacks
)
{
    std::string error_message;
    if (!bluetooth::ble::validate_peripheral_config(config, &error_message)) {
        report_error(callbacks, "configure", -1, error_message);
        return false;
    }
    std::lock_guard lock(impl_->mutex);
    if (impl_->initialized || impl_->started || impl_->advertising) {
        return false;
    }
    impl_->config = bluetooth::ble::normalize_peripheral_config(config);
    impl_->callbacks = std::move(callbacks);
    impl_->configured = true;
    return true;
}

bool BluezPeripheralBackend::clear_callbacks()
{
    std::lock_guard lock(impl_->mutex);
    impl_->callbacks = {};
    return true;
}

bool BluezPeripheralBackend::init(std::string *error_message)
{
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->initialized) {
            return true;
        }
        if (!impl_->configured) {
            if (error_message != nullptr) {
                *error_message = "BLE peripheral is not configured";
            }
            return false;
        }
    }
#if BROOKESIA_HAL_LINUX_BLE_BACKEND_BLUEZ_AVAILABLE
    if (!impl_->initialize(error_message)) {
        return false;
    }
    std::lock_guard lock(impl_->mutex);
    impl_->initialized = true;
    return true;
#else
    if (error_message != nullptr) {
        *error_message = "BlueZ backend was not compiled (gio-2.0 unavailable or stub selected)";
    }
    return false;
#endif
}

bool BluezPeripheralBackend::deinit()
{
    stop();
#if BROOKESIA_HAL_LINUX_BLE_BACKEND_BLUEZ_AVAILABLE
    impl_->shutdown();
#endif
    std::lock_guard lock(impl_->mutex);
    impl_->initialized = false;
    return true;
}

bool BluezPeripheralBackend::start()
{
    bluetooth::ble::PeripheralIface::Callbacks callbacks;
    bool initialized = false;
    {
        std::lock_guard lock(impl_->mutex);
        initialized = impl_->initialized;
        callbacks = impl_->callbacks;
        if (initialized) {
            impl_->started = true;
        }
    }
    if (!initialized) {
        report_error(callbacks, "start", -1, "BlueZ BLE backend is not initialized");
    }
    return initialized;
}

bool BluezPeripheralBackend::stop()
{
    {
        std::lock_guard lock(impl_->mutex);
        impl_->started = false;
    }
    stop_advertising();
    const auto connections = get_connections();
    for (const auto &connection : connections) {
        disconnect(connection.connection_id);
    }
    return true;
}

bool BluezPeripheralBackend::start_advertising()
{
#if BROOKESIA_HAL_LINUX_BLE_BACKEND_BLUEZ_AVAILABLE
    bluetooth::ble::PeripheralIface::Callbacks callbacks;
    bool available = false;
    {
        std::lock_guard lock(impl_->mutex);
        callbacks = impl_->callbacks;
        if (impl_->advertising) {
            return true;
        }
        available = impl_->started && impl_->connections.empty();
        if (available) {
            // Mark the state before the synchronous D-Bus call so an immediately connecting peer is not missed.
            impl_->advertising = true;
        }
    }
    if (!available) {
        report_error(callbacks, "start_advertising", -1, "backend is not started or already connected");
        return false;
    }
    std::string error_message;
    if (!impl_->register_advertisement(&error_message)) {
        {
            std::lock_guard lock(impl_->mutex);
            impl_->advertising = false;
        }
        report_error(callbacks, "start_advertising", -2, error_message);
        return false;
    }
    bool still_advertising = false;
    {
        std::lock_guard lock(impl_->mutex);
        still_advertising = impl_->advertising;
    }
    if (still_advertising && callbacks.on_advertising_state_changed) {
        callbacks.on_advertising_state_changed(true);
    }
    return true;
#else
    return false;
#endif
}

bool BluezPeripheralBackend::stop_advertising()
{
#if BROOKESIA_HAL_LINUX_BLE_BACKEND_BLUEZ_AVAILABLE
    bluetooth::ble::PeripheralIface::Callbacks callbacks;
    bool was_advertising = false;
    {
        std::lock_guard lock(impl_->mutex);
        callbacks = impl_->callbacks;
        was_advertising = impl_->advertising;
    }
    if (!was_advertising) {
        return true;
    }
    std::string error_message;
    if (!impl_->unregister_advertisement(&error_message)) {
        report_error(callbacks, "stop_advertising", -1, error_message);
        return false;
    }
    {
        std::lock_guard lock(impl_->mutex);
        impl_->advertising = false;
    }
    if (callbacks.on_advertising_state_changed) {
        callbacks.on_advertising_state_changed(false);
    }
#endif
    return true;
}

std::vector<bluetooth::ble::ConnectionInfo> BluezPeripheralBackend::get_connections() const
{
    std::lock_guard lock(impl_->mutex);
    std::vector<bluetooth::ble::ConnectionInfo> result;
#if BROOKESIA_HAL_LINUX_BLE_BACKEND_BLUEZ_AVAILABLE
    result.reserve(impl_->connections.size());
    for (const auto &[unused, connection] : impl_->connections) {
        (void)unused;
        result.push_back(connection);
    }
#endif
    return result;
}

bool BluezPeripheralBackend::is_subscribed(
    uint16_t connection_id, const bluetooth::ble::CharacteristicId &characteristic
) const
{
    std::lock_guard lock(impl_->mutex);
#if BROOKESIA_HAL_LINUX_BLE_BACKEND_BLUEZ_AVAILABLE
    const auto *item = impl_->find_characteristic(characteristic);
    return impl_->connections.contains(connection_id) && (item != nullptr) && item->notifying;
#else
    (void)connection_id;
    (void)characteristic;
    return false;
#endif
}

bool BluezPeripheralBackend::notify(
    uint16_t connection_id, const bluetooth::ble::CharacteristicId &characteristic, const bluetooth::ble::ByteArray &data
)
{
#if BROOKESIA_HAL_LINUX_BLE_BACKEND_BLUEZ_AVAILABLE
    bluetooth::ble::PeripheralIface::Callbacks callbacks;
    std::string object_path;
    bool valid = false;
    {
        std::lock_guard lock(impl_->mutex);
        callbacks = impl_->callbacks;
        auto *item = impl_->find_characteristic(characteristic);
        const auto connection = impl_->connections.find(connection_id);
        valid = (connection != impl_->connections.end()) && (item != nullptr) && item->config.notify &&
                item->notifying && (data.size() <= static_cast<size_t>(connection->second.mtu - 3U));
        if (valid) {
            item->value = data;
            object_path = item->path;
        }
    }
    if (!valid) {
        report_error(callbacks, "notify", -1, "invalid connection, subscription, characteristic, or payload");
        return false;
    }

    GVariantBuilder changed;
    g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
    GVariantBuilder value;
    g_variant_builder_init(&value, G_VARIANT_TYPE("ay"));
    for (const auto byte : data) {
        g_variant_builder_add(&value, "y", byte);
    }
    g_variant_builder_add(&changed, "{sv}", "Value", g_variant_builder_end(&value));
    GVariantBuilder invalidated;
    g_variant_builder_init(&invalidated, G_VARIANT_TYPE("as"));
    GError *error = nullptr;
    const bool emitted = g_dbus_connection_emit_signal(
                             impl_->connection, nullptr, object_path.c_str(), "org.freedesktop.DBus.Properties",
                             "PropertiesChanged",
                             g_variant_new(
                                 "(sa{sv}as)", "org.bluez.GattCharacteristic1", &changed, &invalidated
                             ),
                             &error
                         );
    if (!emitted) {
        const std::string message = error != nullptr ? error->message : "unknown D-Bus error";
        g_clear_error(&error);
        report_error(callbacks, "notify", -2, message);
    }
    return emitted;
#else
    (void)connection_id;
    (void)characteristic;
    (void)data;
    return false;
#endif
}

bool BluezPeripheralBackend::disconnect(uint16_t connection_id)
{
#if BROOKESIA_HAL_LINUX_BLE_BACKEND_BLUEZ_AVAILABLE
    bluetooth::ble::PeripheralIface::Callbacks callbacks;
    std::string device_path;
    {
        std::lock_guard lock(impl_->mutex);
        callbacks = impl_->callbacks;
        const auto result = impl_->device_paths.find(connection_id);
        if (result != impl_->device_paths.end()) {
            device_path = result->second;
        }
    }
    if (device_path.empty()) {
        report_error(callbacks, "disconnect", -1, "connection does not exist");
        return false;
    }
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_sync(
                          impl_->connection, Impl::BLUEZ_BUS_NAME, device_path.c_str(), "org.bluez.Device1",
                          "Disconnect", nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error
                      );
    if (reply == nullptr) {
        const std::string message = error != nullptr ? error->message : "unknown BlueZ error";
        g_clear_error(&error);
        report_error(callbacks, "disconnect", -2, message);
        return false;
    }
    g_variant_unref(reply);
    return true;
#else
    (void)connection_id;
    return false;
#endif
}

} // namespace esp_brookesia::hal
