#ifndef VIRTUALFILESYSTEM_H
#define VIRTUALFILESYSTEM_H

#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>
#include "vfs_core.h" // файл зі структурами

class VirtualFileSystem {
private:
    std::fstream disk;
    std::string disk_path;
    std::string log_path;
    Superblock superblock;
    std::vector<FileRecord> file_table;
    std::vector<UserRecord> users_table;
    UserRecord current_user;

    void logAction(const std::string& action, const std::string& details, bool success);

public:
    VirtualFileSystem(const std::string& path) : disk_path(path), log_path("vfs_system.log") {}

    // Основні операції
    bool formatDisk();
    bool mountDisk();

    // Операції з користувачами
    bool loginUser(const std::string& username, const std::string& password);
    bool createUser(const std::string& username, const std::string& password, uint8_t role);
    // Геттер для перевірки поточного користувача
    UserRecord getCurrentUser() const { return current_user; }

    // Операції з файлами (API для Qt)
    bool createFile(const std::string& name, uint32_t size, int32_t parent_dir);
    bool createDirectory(const std::string& name);
    bool deleteFile(const std::string& name, int32_t parent_dir);
    bool deleteDirectory(const std::string& name);
    std::vector<char> readFile(const std::string& name, int32_t parent_dir);
    bool copyFile(const std::string& src_name, int32_t src_parent, const std::string& dest_name, int32_t dest_parent);
    // Операції з метаданими
    bool renameNode(const std::string& old_name, int32_t parent_dir, const std::string& new_name);
    bool moveNode(const std::string& name, int32_t old_parent, int32_t new_parent);

    // Допоміжні методи
    int32_t getNodeIndex(const std::string& name, int32_t parent_dir); // Отримання вмісту каталогу
    std::vector<FileRecord> getDirectoryContent(int32_t parent_dir);
    uint32_t findContiguousFreeSpace(uint32_t required_sectors);
    void saveTables(); // Запис таблиць на диск після змін
};

#endif // VIRTUALFILESYSTEM_H
