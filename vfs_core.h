#ifndef VFS_CORE_H
#define VFS_CORE_H

#include <cstdint>
#include <string>
#include <vector>

const uint32_t DISK_SIZE = 200 * 1024 * 1024; // 200 МБ
const uint32_t SECTOR_SIZE = 512;             // Розмір сектора
const uint32_t TOTAL_SECTORS = DISK_SIZE / SECTOR_SIZE;
const uint32_t MAX_ENTRIES = 2048; // Максимальна кількість файлів/каталогів
const uint32_t MAX_USERS = 16;

// Структура запису в "Таблиці записів розміщення файлів"
#pragma pack(push, 1) // Вимикаємо вирівнювання для точного запису у файл
struct FileRecord {
    char name[32];          // Ім'я файлу або каталогу
    char owner[16];         // Власник (для безпеки)
    uint8_t type;           // 0 - Файл, 1 - Каталог
    uint8_t permissions;    // Права доступу (наприклад, 6 - rw, 4 - r)

    // Ключові поля з рисунка:
    uint32_t start_cluster; // Стартовий сектор даних
    uint32_t size_bytes;    // Розмір у байтах

    int32_t parent_index;   // Індекс батьківського каталогу (-1 для кореня)
    bool is_used;           // Прапорець, чи зайнятий цей запис
};

struct UserRecord {
    char username[16];
    char password_hash[32];
    uint8_t role;           // 0 - Адмін, 1 - Користувач
    bool is_used;
};

// Суперблок містить метадані про саму файлову систему (розташований на початку файлу)
struct Superblock {
    char signature[8];      // Наприклад, "MYFS_1.0"
    uint32_t total_size;
    uint32_t sector_size;
    uint32_t data_start_sector; // З якого сектора починається область даних
};
#pragma pack(pop)

#endif // VFS_CORE_H
