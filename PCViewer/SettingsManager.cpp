#include "SettingsManager.h"

char SettingsManager::settingsFile[] = "settings.cfg";

SettingsManager::SettingsManager()
{
	loadSettings(settingsFile);
}

SettingsManager::~SettingsManager()
{
	storeSettings(settingsFile);
	std::unordered_map<std::string, Setting> c(settings);
	for (auto& s : c) {
		deleteSetting(s.second.id);
	}
}

bool SettingsManager::addSetting(Setting s)
{
	void* data = new char[s.byteLength];
	memcpy(data, s.data, s.byteLength);

	s.data = data;
	settings[s.id] = s;

	if (settingsType.find(s.type) == settingsType.end()) {
		settingsType[s.type] = std::vector<Setting*>();
	}
	settingsType[s.type].push_back(&settings[s.id]);

	return true;
}

bool SettingsManager::deleteSetting(std::string id)
{
	Setting s = settings[id];
	delete[] s.data;

	int i = 0;
	for (; i < settingsType[s.type].size(); i++) {
		if (settingsType[s.type][i]->id == id)
			break;
	}

	settingsType[s.type][i] = settingsType[s.type][settingsType[s.type].size()-1];
	settingsType[s.type].pop_back();

	settings.erase(id);
	return true;
}

SettingsManager::Setting SettingsManager::getSetting(std::string id)
{
	return settings[id];
}

std::vector<SettingsManager::Setting*>* SettingsManager::getSettingsType(std::string type)
{
	return &settingsType[type];
}

void SettingsManager::storeSettings(const char* filename)
{
	std::ofstream file(filename);
	for (auto& s : settings) {
		file << s.second.id << ' ' << s.second.type << ' ' << s.second.byteLength << ' ';
		for (int i = 0; i < s.second.byteLength; i++) {
			file << ((char*)s.second.data)[i];
		}
		file << "\n";
	}
	file.close();
}

void SettingsManager::loadSettings(const char* filename)
{
	std::ifstream file(filename);

	if (!file.is_open()) {
		std::cout << "Settingsfile was not found or no settings exist." << std::endl;
		return;
	}

	while (!file.eof()) {
		//getting the id, type and datasize
		Setting s = {};
		file >> s.id;
		if (s.id.size() == 0)
			break;
		
		file >> s.type;
		file >> s.byteLength;
		s.data = new char[s.byteLength];
		file.seekg(1, file.cur);
		file.read((char*)s.data,s.byteLength);
		file.seekg(1, file.cur);
		if (s.id.size() != 0)
			addSetting(s);
		
		delete[] s.data;
	}

	file.close();
}