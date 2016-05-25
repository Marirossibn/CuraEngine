/** Copyright (C) 2016 Ultimaker - Released under terms of the AGPLv3 License */
#ifndef SETTINGS_SETTING_REGISTRY_H
#define SETTINGS_SETTING_REGISTRY_H

#include <vector>
#include <list>
#include <unordered_map>
#include <string>
#include <iostream> // debug out

#include "SettingConfig.h"
#include "SettingContainer.h"

#include "../utils/NoCopy.h"
#include "rapidjson/document.h"
#include "settings.h"

namespace cura
{

/*!
 * Setting registry.
 * There is a single global setting registry.
 * This registry contains all known setting keys and (some of) their attributes.
 * The default values are stored and retrieved in case a given setting doesn't get a value from the command line or the frontend.
 */
class SettingRegistry : NoCopy
{
private:
    static SettingRegistry instance;

    SettingRegistry();
    
    std::unordered_map<std::string, SettingConfig*> setting_key_to_config; //!< Mapping from setting keys to their configurations

    SettingContainer setting_definitions; //!< All setting configurations (A flat list)
    
    std::vector<SettingContainer> extruder_trains; //!< The setting overrides per extruder train as defined in the json file

public:
    /*!
     * Get the SettingRegistry.
     * 
     * This is a singleton class.
     * 
     * \return The SettingRegistry
     */
    static SettingRegistry* getInstance() { return &instance; }
    
    bool settingExists(std::string key) const;
    SettingConfig* getSettingConfig(std::string key) const;

    /*!
     * Retrieve the setting definitions container for all settings of a given extruder train.
     * 
     * \param extruder_nr The extruder train to retrieve
     * \return The extruder train or nullptr if \p extruder_nr refers to an extruder train which is undefined in the json.
     */
    SettingContainer* getExtruderTrain(unsigned int extruder_nr);
protected:
    /*!
     * Whether this json settings object is a definition of a CuraEngine setting,
     * or only a shorthand setting to control other settings.
     * Only settings used by the engine will be recordedd in the registry.
     * 
     * \param setting The setting to check whether CuraEngine uses it.
     * \return Whether CuraEngine uses the setting.
     */
    bool settingIsUsedByEngine(const rapidjson::Value& setting);

    /*!
     * Get the filename for the machine definition with the given id.
     * Also search the parent directory of \p parent_file.
     * Check the directories in CURA_ENGINE_SEARCH_PATH (environment var).
     * 
     * \param machine_id The id and base filename (without extensions) of the machine definition to search for.
     * \param parent_file A file probably at the same location of the file to be found.
     * \param result The filename of the machine definition
     * \return Whether we found the file.
     */
    static bool getDefinitionFile(const std::string machine_id, const std::string parent_file, std::string& result);
    
    /*!
     * Get the default value of a setting
     * 
     * \param json_object_it An iterator for a given setting json object
     * \return The default vlaue as stored internally (rather than as stored in the json file)
     */
    static std::string getDefault(const rapidjson::GenericValue< rapidjson::UTF8< char > >::ConstMemberIterator& json_object_it);
public:
    bool settingsLoaded() const;
    /*!
     * Load settings from a json file and all the parents it inherits from.
     * 
     * Uses recursion to load the parent json file.
     * 
     * \param filename The filename of the json file to parse
     * \param settings_base The settings base where to store the default values.
     * \param overload_defaults_only whether to make new setting definitions, or override existing ones
     * \return an error code or zero of succeeded
     */
    int loadJSONsettings(std::string filename, SettingsBase* settings_base, bool overload_defaults_only);
    
    void debugOutputAllSettings() const
    {
        setting_definitions.debugOutputAllSettings();
    }
    
private:
    
    /*!
     * \param type type to convert to string
     * \return human readable version of json type
     */
    static std::string toString(rapidjson::Type type);
public:
    /*!
     * Load a json document.
     * 
     * \param filename The filename of the json file to parse
     * \param json_document (output) the document to be loaded
     * \return an error code or zero of succeeded
     */
    static int loadJSON(std::string filename, rapidjson::Document& json_document);
private:
    /*!
     * Load settings from a single json file.
     * 
     * \param filename The filename of the json file to parse
     * \param settings_base The settings base where to store the default values.
     * \param warn_duplicates whether to warn for duplicate definitions
     * \param overload_defaults_only whether to make new setting definitions, or override existing ones
     * \return an error code or zero of succeeded
     */
    int loadJSONsettingsFromDoc(rapidjson::Document& json_document, SettingsBase* settings_base, bool warn_duplicates, bool overload_defaults_only);
    
    /*!
     * Get the string from a json value (generally the default value field of a setting)
     * \param dflt The value to convert to string
     * \param setting_name The name of the setting (in case we need to display an error message)
     * \return The string
     */
    static std::string toString(const rapidjson::Value& dflt, std::string setting_name = "?");

    /*!
     * Create a new SettingConfig and add it to the registry.
     * 
     * \param name The internal key of the setting
     * \param label The human readable name for the frontend
     * \return The config created
     */
    SettingConfig& addSetting(std::string name, std::string label);

    /*!
     * Load inessential data about the setting, like its type and unit.
     * 
     * \param[out] config Where to store the data
     * \param[in] json_object_it Iterator to a setting json object
     */
    void _loadSettingValues(SettingConfig* config, const rapidjson::Value::ConstMemberIterator& json_object_it);

    /*!
     * Handle a json object which contains a list of settings.
     * 
     * \param settings_list The object containing one or more setting definitions
     * \param path The path of (internal) setting names traversed to get to this object
     * \param settings_base The settings base where to store the default values.
     * \param warn_duplicates whether to warn for duplicate setting definitions
     * \param overload_defaults_only whether to make new setting definitions, or override existing ones
     */
    void handleChildren(const rapidjson::Value& settings_list, std::list<std::string>& path, SettingsBase* settings_base, bool warn_duplicates, bool overload_defaults_only);
    
    /*!
     * Handle a json object for a setting.
     * 
     * \param json_setting_it Iterator for the setting which contains the key (setting name) and attributes info
     * \param path The path of (internal) setting names traversed to get to this object
     * \param settings_base The settings base where to store the default values.
     * \param warn_duplicates whether to warn for duplicate setting definitions
     * \param overload_defaults_only whether to make new setting definitions, or override existing ones
     */
    void handleSetting(const rapidjson::Value::ConstMemberIterator& json_setting_it, std::list<std::string>& path, SettingsBase* settings_base, bool warn_duplicates, bool overload_defaults_only);
};

}//namespace cura
#endif//SETTINGS_SETTING_REGISTRY_H
