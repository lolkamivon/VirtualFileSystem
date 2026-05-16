#include "virtualfilesystem.h"
#include <vector>
#include <cstring>
#include <iostream>

// Форматування: створення 200МБ файлу та ініціалізація структур
bool VirtualFileSystem::formatDisk() {
    // Відкриваємо файл для бінарного запису, очищаючи його (trunc)
    std::ofstream disk(disk_path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!disk.is_open()) {
        return false;
    }

    // 1. Ініціалізація Суперблоку
    memset(&superblock, 0, sizeof(Superblock));
    strncpy(superblock.signature, "MYFS_1.0", 8);
    superblock.total_size = DISK_SIZE;
    superblock.sector_size = SECTOR_SIZE;

    // Обчислюємо, де починаються самі дані (після всіх таблиць)
    uint32_t tables_size = sizeof(Superblock) + (MAX_USERS * sizeof(UserRecord)) + (MAX_ENTRIES * sizeof(FileRecord));
    // Вирівнюємо початок даних по межі сектора (512 байт)
    superblock.data_start_sector = (tables_size / SECTOR_SIZE) + 1;

    // Записуємо Суперблок на самий початок
    disk.write(reinterpret_cast<char*>(&superblock), sizeof(Superblock));

    // 2. Створення таблиці користувачів (за замовчуванням створюємо "admin")
    users_table.assign(MAX_USERS, UserRecord{}); // Заповнюємо нулями
    users_table[0].is_used = true;
    strncpy(users_table[0].username, "admin", 16);
    strncpy(users_table[0].password_hash, "admin123", 32); // У реальному проєкті тут має бути хеш
    users_table[0].role = 0; // 0 - Адмін

    disk.write(reinterpret_cast<char*>(users_table.data()), MAX_USERS * sizeof(UserRecord));

    // 3. Створення порожньої таблиці розміщення файлів
    file_table.assign(MAX_ENTRIES, FileRecord{}); // Усі is_used = false автоматично
    disk.write(reinterpret_cast<char*>(file_table.data()), MAX_ENTRIES * sizeof(FileRecord));

    // 4. Фізичне виділення 200 МБ під файл на жорсткому диску Linux
    // Замість запису 200МБ нулів, ми просто стрибаємо в кінець і записуємо один байт.
    // ОС сама зарезервує потрібний простір (створить sparse file або заповнить нулями).
    disk.seekp(DISK_SIZE - 1);
    char empty_byte = 0;
    disk.write(&empty_byte, 1);

    disk.close();
    logAction("Форматування", "Диск відформатовано. Розмір: " + std::to_string(DISK_SIZE) + " байт", true);
    return true;

}

// Монтування: зчитування існуючої ФС при запуску програми
bool VirtualFileSystem::mountDisk() {
    std::ifstream disk(disk_path, std::ios::binary | std::ios::in);
    if (!disk.is_open()) {
        return false; // Файлу не існує, потрібно форматувати
    }

    // Зчитуємо Суперблок
    disk.read(reinterpret_cast<char*>(&superblock), sizeof(Superblock));

    // Перевіряємо сигнатуру, щоб переконатися, що це наша ФС
    if (strncmp(superblock.signature, "MYFS_1.0", 8) != 0) {
        disk.close();
        return false; // Пошкоджений або чужий файл
    }

    // Зчитуємо таблиці в оперативну пам'ять для швидкої роботи
    users_table.resize(MAX_USERS);
    disk.read(reinterpret_cast<char*>(users_table.data()), MAX_USERS * sizeof(UserRecord));

    file_table.resize(MAX_ENTRIES);
    disk.read(reinterpret_cast<char*>(file_table.data()), MAX_ENTRIES * sizeof(FileRecord));

    disk.close();
    return true;
}

uint32_t VirtualFileSystem::findContiguousFreeSpace(uint32_t size_bytes) {
    if (size_bytes == 0) return 0;

    // 1. Обчислюємо, скільки секторів нам потрібно
    // Формула з округленням вгору: (розмір + розмір_сектора - 1) / розмір_сектора
    uint32_t required_sectors = (size_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;

    // 2. Створюємо карту диска (false - вільно, true - зайнято)
    // Розмір: 200 МБ / 512 байт = 409 600 елементів (займає дуже мало в ОЗП)
    std::vector<bool> used_sectors(TOTAL_SECTORS, false);

    // Захищаємо системну область (суперблок і таблиці записів)
    for (uint32_t i = 0; i < superblock.data_start_sector; ++i) {
        used_sectors[i] = true;
    }

    // Відмічаємо сектори, які вже зайняті існуючими файлами
    for (const auto& file : file_table) {
        if (file.is_used && file.type == 0) { // Якщо це файл (каталоги не займають дані на диску в цій моделі)
            uint32_t file_sectors = (file.size_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
            for (uint32_t i = 0; i < file_sectors; ++i) {
                if (file.start_cluster + i < TOTAL_SECTORS) {
                    used_sectors[file.start_cluster + i] = true;
                }
            }
        }
    }

    // 3. Алгоритм пошуку "First Fit" (Перший підходящий)
    uint32_t free_count = 0;
    uint32_t start_candidate = 0;

    for (uint32_t i = superblock.data_start_sector; i < TOTAL_SECTORS; ++i) {
        if (!used_sectors[i]) {
            if (free_count == 0) {
                start_candidate = i; // Запам'ятовуємо початок вільного блоку
            }
            free_count++;

            // Якщо знайшли достатньо суцільного місця - повертаємо результат
            if (free_count == required_sectors) {
                return start_candidate;
            }
        } else {
            free_count = 0; // Зіткнулися із зайнятим сектором, скидаємо лічильник
        }
    }

    // Якщо цикл завершився і ми тут, значить суцільного шматка такого розміру немає
    return 0;
}

void VirtualFileSystem::saveTables() {
    // Відкриваємо файл для читання/запису без його очищення
    std::fstream disk(disk_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!disk.is_open()) return;

    // Зміщуємось туди, де починаються таблиці (одразу після Суперблоку)
    disk.seekp(sizeof(Superblock));

    // Перезаписуємо масиви
    disk.write(reinterpret_cast<char*>(users_table.data()), MAX_USERS * sizeof(UserRecord));
    disk.write(reinterpret_cast<char*>(file_table.data()), MAX_ENTRIES * sizeof(FileRecord));

    disk.close();
}

bool VirtualFileSystem::createDirectory(const std::string& name) {
    if (name.empty() || name.length() >= 32) return false;

    // Шукаємо вільний рядок у таблиці
    int32_t free_index = -1;
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (!file_table[i].is_used) {
            free_index = i;
            break;
        }
    }
    if (free_index == -1) return false;

    // Заповнюємо запис для каталогу
    file_table[free_index].is_used = true;
    strncpy(file_table[free_index].name, name.c_str(), 31);
    file_table[free_index].name[31] = '\0';
    strncpy(file_table[free_index].owner, current_user.username, 15);
    file_table[free_index].owner[15] = '\0';

    file_table[free_index].type = 1;            // 1 - це каталог
    file_table[free_index].permissions = 7;     // rwx
    file_table[free_index].start_cluster = 0;   // Каталог не має тіла з даними
    file_table[free_index].size_bytes = 0;

    // Всі каталоги створюються в корені, тому parent_index = -1
    file_table[free_index].parent_index = -1;

    saveTables();
    logAction("Створення каталогу", "Ім'я: " + name, true);
    return true;
}

bool VirtualFileSystem::createFile(const std::string& name, uint32_t size, int32_t parent_dir) {
    // Базові перевірки
    if (size == 0 || name.empty() || name.length() >= 32) return false;

    // 1. Шукаємо вільний рядок у таблиці файлів (FileRecord)
    int32_t free_index = -1;
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (!file_table[i].is_used) {
            free_index = i;
            break;
        }
    }
    if (free_index == -1) return false; // Таблиця переповнена (більше 2048 об'єктів)

    // 2. Перевіряємо, чи немає вже файлу з таким ім'ям у цій папці
    for (const auto& file : file_table) {
        if (file.is_used && file.parent_index == parent_dir && std::string(file.name) == name) {
            return false; // Файл вже існує
        }
    }

    // 3. Шукаємо вільні суміжні кластери на диску
    uint32_t start_sector = findContiguousFreeSpace(size);

    if (start_sector == 0) {// Недостатньо безперервного місця на диску
        logAction("Створення файлу", "Ім'я: " + name + " (Недостатньо місця)", false);
        return false;
    }

    // 4. Записуємо дані у знайдений вільний рядок таблиці
    file_table[free_index].is_used = true;
    strncpy(file_table[free_index].name, name.c_str(), 31);
    file_table[free_index].name[31] = '\0'; // Гарантуємо нуль-термінатор

    // Тимчасово записуємо власником адміна (поки не підключили авторизацію)
    strncpy(file_table[free_index].owner, current_user.username, 15);
    file_table[free_index].owner[15] = '\0';

    file_table[free_index].type = 0;            // 0 - це файл
    file_table[free_index].permissions = 6;     // Права доступу (наприклад, rw-)
    file_table[free_index].start_cluster = start_sector;
    file_table[free_index].size_bytes = size;
    file_table[free_index].parent_index = parent_dir;

    // 5. Очищуємо виділене місце на диску (заповнюємо нулями)
    std::fstream disk(disk_path, std::ios::binary | std::ios::in | std::ios::out);
    if (disk.is_open()) {
        disk.seekp(start_sector * SECTOR_SIZE);
        std::vector<char> empty_data(size, 0); // Створюємо буфер з нулів розміром з файл
        disk.write(empty_data.data(), size);
        disk.close();
    }

    // 6. Зберігаємо оновлену таблицю на диск
    saveTables();

    logAction("Створення файлу", "Ім'я: " + name + ", Розмір: " + std::to_string(size) + " байт", true);
    return true;
}

bool VirtualFileSystem::deleteFile(const std::string& name, int32_t parent_dir) { // видалення файлів
    if (name.empty()) return false;

    int32_t target_index = -1;

    // Шукаємо файл у таблиці
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (file_table[i].is_used &&
            file_table[i].type == 0 && // Перевіряємо, що це саме файл
            file_table[i].parent_index == parent_dir &&
            std::string(file_table[i].name) == name) {

            target_index = i;
            break;
        }
    }

    // Якщо файл не знайдено
    if (target_index == -1) return false;

    // ---> ДОДАЄМО ПЕРЕВІРКУ БЕЗПЕКИ <---
    if (current_user.role != 0 && std::string(file_table[target_index].owner) != current_user.username) {
        logAction("Видалення", "Відмова у доступі до файлу: " + name, false);
        return false;
    }

    // "Видаляємо" файл
    file_table[target_index].is_used = false;

    // "Видаляємо" файл, просто позначаючи його запис як вільний
    file_table[target_index].is_used = false;

    saveTables();

    logAction("Видалення", "Файл успішно видалено: " + name, true);

    return true;
}

bool VirtualFileSystem::deleteDirectory(const std::string& name) { // видалення каталогів
    if (name.empty()) return false;

    int32_t target_index = -1;

    // Шукаємо каталог у корені (parent_index == -1)
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (file_table[i].is_used &&
            file_table[i].type == 1 && // Перевіряємо, що це каталог
            file_table[i].parent_index == -1 &&
            std::string(file_table[i].name) == name) {

            target_index = i;
            break;
        }
    }

    if (target_index == -1) return false; // Каталог не знайдено

    // ---> ДОДАЄМО ПЕРЕВІРКУ БЕЗПЕКИ <---
    if (current_user.role != 0 && std::string(file_table[target_index].owner) != current_user.username) {
        return false; // Відмова в доступі
    }

    // Перевіряємо, чи каталог порожній
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        // Якщо є хоча б один файл, у якого parent_index вказує на цей каталог
        if (file_table[i].is_used && file_table[i].parent_index == target_index) {
            return false; // Помилка: каталог не порожній
        }
    }

    // Якщо перевірка пройдена, видаляємо каталог
    file_table[target_index].is_used = false;

    saveTables();
    return true;
}

std::vector<char> VirtualFileSystem::readFile(const std::string& name, int32_t parent_dir) { // Читання файлів
    std::vector<char> data; // Порожній буфер за замовчуванням
    if (name.empty()) return data;

    int32_t target_index = -1;

    // 1. Шукаємо файл у таблиці
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (file_table[i].is_used &&
            file_table[i].type == 0 &&
            file_table[i].parent_index == parent_dir &&
            std::string(file_table[i].name) == name) {

            target_index = i;
            break;
        }
    }

    if (target_index == -1) return data; // Файл не знайдено

    // 2. Отримуємо координати
    uint32_t start_sector = file_table[target_index].start_cluster;
    uint32_t size = file_table[target_index].size_bytes;

    if (size == 0) return data; // Файл порожній

    // 3. Зчитуємо дані з фізичного диска
    std::ifstream disk(disk_path, std::ios::binary);
    if (disk.is_open()) {
        disk.seekg(start_sector * SECTOR_SIZE); // Стрибаємо на початок файлу
        data.resize(size);                      // Виділяємо пам'ять під розмір файлу
        disk.read(data.data(), size);           // Читаємо все одним шматком
        disk.close();
    }

    return data;
}

// Перейменування файлу або каталогу
bool VirtualFileSystem::renameNode(const std::string& old_name, int32_t parent_dir, const std::string& new_name) {
    if (new_name.empty() || new_name.length() >= 32) return false;

    int32_t target_index = -1;

    // 1. Шукаємо об'єкт для перейменування
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (file_table[i].is_used && file_table[i].parent_index == parent_dir &&
            std::string(file_table[i].name) == old_name) {
            target_index = i;
            break;
        }
    }
    if (target_index == -1) return false; // Об'єкт не знайдено

    // ---> ЛОГІКА БЕЗПЕКИ <---
    // Якщо поточний користувач НЕ адмін і НЕ власник файлу - забороняємо
    if (current_user.role != 0 && std::string(file_table[target_index].owner) != current_user.username) {
        return false;
    }

    // 2. Перевіряємо, чи немає вже об'єкта з новим іменем у цій же папці
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (file_table[i].is_used && file_table[i].parent_index == parent_dir &&
            std::string(file_table[i].name) == new_name) {
            return false; // Ім'я вже зайняте
        }
    }

    // 3. Змінюємо ім'я
    strncpy(file_table[target_index].name, new_name.c_str(), 31);
    file_table[target_index].name[31] = '\0'; // Гарантуємо відсутність "сміття"

    saveTables();
    logAction("Перейменування", old_name + " -> " + new_name, true);
    return true;
}

// Переміщення файлу в інший каталог
bool VirtualFileSystem::moveNode(const std::string& name, int32_t old_parent, int32_t new_parent) {
    int32_t target_index = -1;
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (file_table[i].is_used && file_table[i].parent_index == old_parent && std::string(file_table[i].name) == name) {
            target_index = i; break;
        }
    }
    if (target_index == -1) return false;

    // --- БЕЗПЕКА ---
    if (current_user.role != 0 && std::string(file_table[target_index].owner) != current_user.username) {
        logAction("Переміщення", "Відмова у доступі: " + name, false);
        return false;
    }

    // Перевіряємо конфлікт імен
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (file_table[i].is_used && file_table[i].parent_index == new_parent && std::string(file_table[i].name) == name) {
            return false;
        }
    }

    file_table[target_index].parent_index = new_parent;
    saveTables();

    logAction("Переміщення", "Файл: " + name + " переміщено", true);
    return true;
}

// Копіювання файлу
bool VirtualFileSystem::copyFile(const std::string& src_name, int32_t src_parent, const std::string& dest_name, int32_t dest_parent) {
    int32_t src_index = -1;
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (file_table[i].is_used && file_table[i].type == 0 && file_table[i].parent_index == src_parent && std::string(file_table[i].name) == src_name) {
            src_index = i; break;
        }
    }
    if (src_index == -1) return false;

    // --- БЕЗПЕКА ---
    if (current_user.role != 0 && std::string(file_table[src_index].owner) != current_user.username) {
        logAction("Копіювання", "Відмова у доступі: " + src_name, false);
        return false;
    }

    // Пошук вільного рядка та місця на диску (скорочено для наочності, код такий же)
    int32_t dest_index = -1;
    for (int i = 0; i < MAX_ENTRIES; ++i) { if (!file_table[i].is_used) { dest_index = i; break; } }
    if (dest_index == -1) return false;

    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (file_table[i].is_used && file_table[i].parent_index == dest_parent && std::string(file_table[i].name) == dest_name) return false;
    }

    uint32_t size = file_table[src_index].size_bytes;
    uint32_t dest_start_sec = findContiguousFreeSpace(size);
    if (dest_start_sec == 0) return false;

    // Фізичне копіювання
    std::fstream disk(disk_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!disk.is_open()) return false;
    std::vector<char> buffer(size);
    disk.seekg(file_table[src_index].start_cluster * SECTOR_SIZE);
    disk.read(buffer.data(), size);
    disk.seekp(dest_start_sec * SECTOR_SIZE);
    disk.write(buffer.data(), size);
    disk.close();

    // Запис метаданих
    file_table[dest_index] = file_table[src_index];
    strncpy(file_table[dest_index].name, dest_name.c_str(), 31);
    file_table[dest_index].name[31] = '\0';
    file_table[dest_index].parent_index = dest_parent;
    file_table[dest_index].start_cluster = dest_start_sec;

    // Власником копії стає той, хто її створив
    strncpy(file_table[dest_index].owner, current_user.username, 15);
    file_table[dest_index].owner[15] = '\0';

    saveTables();
    logAction("Копіювання", src_name + " -> " + dest_name, true);
    return true;
}
// Вхід у систему
bool VirtualFileSystem::loginUser(const std::string& username, const std::string& password) {
    for (int i = 0; i < MAX_USERS; ++i) {
        if (users_table[i].is_used && std::string(users_table[i].username) == username && std::string(users_table[i].password_hash) == password) {
            current_user = users_table[i];
            logAction("Авторизація", "Вхід виконано", true);
            return true;
        }
    }
    // Оскільки ми не знаємо, чи це SYSTEM чи хтось інший зараз намагається зайти, лог трохи інший
    logAction("Авторизація", "Невдала спроба входу для логіна: " + username, false);
    return false;
}


// Створення нового користувача
bool VirtualFileSystem::createUser(const std::string& username, const std::string& password, uint8_t role) {
    // 1. Перевірка безпеки: створювати користувачів може ТІЛЬКИ адміністратор (роль 0)
    if (current_user.role != 0) {
        return false; // Немає прав доступу
    }

    if (username.empty() || username.length() >= 16 || password.length() >= 32) return false;

    // 2. Перевіряємо, чи немає вже такого користувача
    for (int i = 0; i < MAX_USERS; ++i) {
        if (users_table[i].is_used && std::string(users_table[i].username) == username) {
            return false; // Користувач вже існує
        }
    }

    // 3. Шукаємо вільний слот
    int32_t free_index = -1;
    for (int i = 0; i < MAX_USERS; ++i) {
        if (!users_table[i].is_used) {
            free_index = i;
            break;
        }
    }
    if (free_index == -1) return false; // Досягнуто ліміт (MAX_USERS)

    // 4. Записуємо нового користувача
    users_table[free_index].is_used = true;
    strncpy(users_table[free_index].username, username.c_str(), 15);
    users_table[free_index].username[15] = '\0';
    strncpy(users_table[free_index].password_hash, password.c_str(), 31);
    users_table[free_index].password_hash[31] = '\0';
    users_table[free_index].role = role;

    saveTables();
    return true;
}

std::vector<FileRecord> VirtualFileSystem::getDirectoryContent(int32_t parent_dir) {
    std::vector<FileRecord> content;
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        // Перевіряємо, чи запис існує і чи лежить він у потрібній папці
        if (file_table[i].is_used && file_table[i].parent_index == parent_dir) {

            // ЛОГІКА БЕЗПЕКИ:
            // Якщо користувач - адміністратор (роль 0) АБО він є власником цього файлу/папки
            if (current_user.role == 0 || std::string(file_table[i].owner) == std::string(current_user.username)) {
                content.push_back(file_table[i]);
            }
        }
    }
    return content;
}

int32_t VirtualFileSystem::getNodeIndex(const std::string& name, int32_t parent_dir) {
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (file_table[i].is_used && file_table[i].parent_index == parent_dir &&
            std::string(file_table[i].name) == name) {
            return i;
        }
    }
    return -1; // Не знайдено
}

void VirtualFileSystem::logAction(const std::string& action, const std::string& details, bool success) {
    // Відкриваємо файл у режимі "app" (append), щоб дописувати в кінець
    std::ofstream log_file(log_path, std::ios::app);
    if (!log_file.is_open()) return;

    // Отримуємо поточний системний час
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);

    // Записуємо час
    log_file << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "] ";

    // Записуємо користувача (якщо хтось вже залогінився, інакше пишемо SYSTEM)
    std::string user = current_user.is_used ? current_user.username : "SYSTEM";
    log_file << "[User: " << user << "] ";

    // Записуємо дію та її статус (Успішно / Помилка)
    log_file << action << " | " << details << " | Статус: "
             << (success ? "УСПІХ" : "ВІДХИЛЕНО") << "\n";

    log_file.close();
}
