#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QInputDialog>
#include <QMessageBox>
#include <QString>

// У конструкторі ми одразу ініціалізуємо об'єкт vfs передаючи йому шлях до файлу-контейнера
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , vfs("my_virtual_disk.bin") // Назва файлу на реальному Linux-диску
    , current_dir_index(-1)      // Починаємо роботу з кореневого каталогу
{
    ui->setupUi(this);

    // Спроба змонтувати диск. Якщо його немає або він пошкоджений - форматуємо.
    if (!vfs.mountDisk()) {
        QMessageBox::information(this, "Ініціалізація", "Диск не знайдено. Виконується форматування...");
        vfs.formatDisk();
        vfs.mountDisk();
    }

    // Тимчасовий авто-логін адміністратором для зручності розробки
    vfs.loginUser("admin", "admin123");

    refreshUI();
}

MainWindow::~MainWindow() {
    delete ui;
}

// Універсальна функція для перемальовування списку файлів
void MainWindow::refreshUI() {
    ui->fileListWidget->clear();

    // Оновлюємо інформацію про користувача
    ui->lblUser->setText(QString("Користувач: %1").arg(vfs.getCurrentUser().username));

    // Якщо ми не в корені, додаємо пункт ".." для повернення назад
    if (current_dir_index != -1) {
        ui->fileListWidget->addItem("📁 .. (Назад)");
    }

    // Отримуємо файли для поточної папки з нашого бекенду
    std::vector<FileRecord> content = vfs.getDirectoryContent(current_dir_index);

    for (const auto& record : content) {
        QString name = QString::fromUtf8(record.name);
        QString typeIcon = (record.type == 1) ? "📁 " : "📄 ";
        QString sizeStr = (record.type == 0) ? QString(" (%1 байт)").arg(record.size_bytes) : "";

        ui->fileListWidget->addItem(typeIcon + name + sizeStr);
    }
}

// Обробка натискання кнопки "Створити папку"
void MainWindow::on_btnCreateDir_clicked() {
    // Папки можна створювати ТІЛЬКИ в корені (згідно з дворівневою архітектурою)
    if (current_dir_index != -1) {
        QMessageBox::warning(this, "Помилка", "У дворівневій ФС папки створюються лише в кореневому каталозі!");
        return;
    }

    bool ok;
    QString dirName = QInputDialog::getText(this, "Нова папка", "Введіть назву каталогу:", QLineEdit::Normal, "", &ok);

    if (ok && !dirName.isEmpty()) {
        if (vfs.createDirectory(dirName.toStdString())) {
            refreshUI();
        } else {
            QMessageBox::critical(this, "Помилка", "Не вдалося створити каталог. Можливо, таке ім'я вже існує або немає місця.");
        }
    }
}

// Обробка натискання кнопки "Створити файл"
void MainWindow::on_btnCreateFile_clicked() {
    // Файли логічно створювати всередині папок користувачів, а не в корені
    if (current_dir_index == -1) {
        QMessageBox::warning(this, "Помилка", "Будь ласка, зайдіть у папку користувача, щоб створити файл.");
        return;
    }

    bool ok1, ok2;
    QString fileName = QInputDialog::getText(this, "Новий файл", "Введіть ім'я файлу:", QLineEdit::Normal, "test.txt", &ok1);
    if (!ok1 || fileName.isEmpty()) return;

    int fileSize = QInputDialog::getInt(this, "Розмір", "Введіть розмір файлу в байтах:", 1024, 1, 200*1024*1024, 1, &ok2);
    if (!ok2) return;

    if (vfs.createFile(fileName.toStdString(), fileSize, current_dir_index)) {
        refreshUI();
    } else {
        QMessageBox::critical(this, "Помилка", "Не вдалося створити файл. Немає суцільного вільного місця або конфлікт імен.");
    }
}

// Обробка натискання кнопки "Форматувати"
void MainWindow::on_btnFormat_clicked() {
    // Перевірка прав
    if (vfs.getCurrentUser().role != 0) {
        QMessageBox::critical(this, "Відмовлено", "Тільки адміністратор має право форматувати диск!");
        return;
    }
    // Запитуємо підтвердження у користувача
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Увага", "Відформатувати віртуальний диск? Всі дані будуть знищені!", QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        if (vfs.formatDisk()) {
            vfs.mountDisk();
            vfs.loginUser("admin", "admin123"); // Перезаходимо як адмін
            current_dir_index = -1;             // Повертаємось у корінь
            refreshUI();
            QMessageBox::information(this, "Форматування", "Диск успішно відформатовано!");
        } else {
            QMessageBox::critical(this, "Помилка", "Не вдалося відформатувати диск.");
        }
    }
}

// Обробка натискання кнопки "Видалити"
void MainWindow::on_btnDelete_clicked() {
    QListWidgetItem *item = ui->fileListWidget->currentItem();
    if (!item) {
        QMessageBox::warning(this, "Увага", "Спочатку оберіть файл або каталог для видалення!");
        return;
    }

    QString text = item->text();
    if (text == "📁 .. (Назад)") return; // Забороняємо видаляти кнопку "Назад"

    // Витягуємо ім'я (емодзі займає 2 символи + 1 пробіл = відрізаємо перші 3 символи)
    QString name = text.mid(3).split(" (").first();

    if (text.startsWith("📁 ")) {
        // Це папка
        if (vfs.deleteDirectory(name.toStdString())) {
            refreshUI();
        } else {
            QMessageBox::critical(this, "Помилка", "Не вдалося видалити каталог. Можливо, він не порожній.");
        }
    } else {
        // Це файл
        if (vfs.deleteFile(name.toStdString(), current_dir_index)) {
            refreshUI();
        } else {
            QMessageBox::critical(this, "Помилка", "Не вдалося видалити файл.");
        }
    }
}

// Навігація по папках та читання файлів (подвійний клік по списку)
void MainWindow::on_fileListWidget_doubleClicked(const QModelIndex &index) {
    QString text = ui->fileListWidget->item(index.row())->text();

    // Якщо натиснули "Назад"
    if (text == "📁 .. (Назад)") {
        current_dir_index = -1; // Повертаємось у корінь
        refreshUI();
        return;
    }

    // Якщо натиснули на ПАПКУ
    if (text.startsWith("📁 ")) {
        QString name = text.mid(3).split(" (").first();
        int32_t new_dir_id = vfs.getNodeIndex(name.toStdString(), current_dir_index);

        if (new_dir_id != -1) {
            current_dir_index = new_dir_id; // Заходимо всередину
            refreshUI();
        }
    }
    // Якщо натиснули на ФАЙЛ
    else if (text.startsWith("📄 ")) {
        QString name = text.mid(3).split(" (").first();

        // Зчитуємо масив байтів з диска
        std::vector<char> fileData = vfs.readFile(name.toStdString(), current_dir_index);

        if (fileData.empty()) {
            QMessageBox::information(this, "Читання", "Файл порожній.");
            return;
        }

        // Перевіряємо, чи файл не заповнений нулями (як це буває при нашому базовому створенні)
        bool isEmptySpace = true;
        for(char c : fileData) {
            if (c != 0) { isEmptySpace = false; break; }
        }

        if (isEmptySpace) {
            QMessageBox::information(this, "Читання",
                QString("Файл '%1' успішно прочитано.\nРозмір: %2 байт.\nВміст: [Порожні байти / Нулі]").arg(name).arg(fileData.size()));
        } else {
            // Якщо там є реальний текст (на майбутнє, коли ми додамо запис тексту)
            QString fileContent = QString::fromUtf8(fileData.data(), fileData.size());
            QMessageBox::information(this, "Вміст файлу " + name, fileContent);
        }
    }
}

// Обробка кнопки "Створити користувача"
void MainWindow::on_btnCreateUser_clicked() {
    // 1. Перевіряємо, чи поточний користувач є адміністратором (роль 0)
    if (vfs.getCurrentUser().role != 0) {
        QMessageBox::warning(this, "Відмовлено", "Тільки адміністратор може створювати нових користувачів!");
        return;
    }

    bool ok1, ok2;
    // 2. Запитуємо логін
    QString username = QInputDialog::getText(this, "Новий користувач", "Введіть логін:", QLineEdit::Normal, "", &ok1);
    if (!ok1 || username.isEmpty()) return;

    // 3. Запитуємо пароль
    QString password = QInputDialog::getText(this, "Новий користувач", "Введіть пароль:", QLineEdit::Password, "", &ok2);
    if (!ok2 || password.isEmpty()) return;

    // 4. Створюємо користувача з роллю 1 (звичайний користувач)
    if (vfs.createUser(username.toStdString(), password.toStdString(), 1)) {
        QMessageBox::information(this, "Успіх", "Користувача успішно створено!");
    } else {
        QMessageBox::critical(this, "Помилка", "Не вдалося створити. Можливо, такий логін вже існує або немає місця.");
    }
}

// Обробка кнопки "Змінити користувача" (Авторизація)
void MainWindow::on_btnChangeUser_clicked() {
    bool ok1, ok2;

    // 1. Запитуємо логін
    QString username = QInputDialog::getText(this, "Авторизація", "Введіть логін:", QLineEdit::Normal, "", &ok1);
    if (!ok1 || username.isEmpty()) return;

    // 2. Запитуємо пароль (символи будуть приховані зірочками завдяки QLineEdit::Password)
    QString password = QInputDialog::getText(this, "Авторизація", "Введіть пароль:", QLineEdit::Password, "", &ok2);
    if (!ok2 || password.isEmpty()) return;

    // 3. Спроба входу
    if (vfs.loginUser(username.toStdString(), password.toStdString())) {
        current_dir_index = -1; // При зміні користувача викидаємо його в корінь диска
        refreshUI();            // Оновлюємо інтерфейс (ім'я користувача зверху зміниться)
        QMessageBox::information(this, "Вхід", "Ви успішно увійшли як " + username);
    } else {
        QMessageBox::warning(this, "Помилка", "Неправильний логін або пароль!");
    }
}

// Обробка натискання кнопки "Перейменувати"
void MainWindow::on_btnRename_clicked() {
    // 1. Отримуємо виділений елемент зі списку
    QListWidgetItem *item = ui->fileListWidget->currentItem();
    if (!item) {
        QMessageBox::warning(this, "Увага", "Спочатку оберіть файл або каталог для перейменування!");
        return;
    }

    QString text = item->text();

    // Забороняємо перейменовувати системну кнопку "Назад"
    if (text == "📁 .. (Назад)") return;

    // 2. Витягуємо чисте старе ім'я (відрізаємо емодзі та пробіл, а також розмір у байтах)
    QString oldName = text.mid(3).split(" (").first();

    // 3. Запитуємо у користувача нове ім'я через діалогове вікно
    bool ok;
    QString newName = QInputDialog::getText(this, "Перейменування",
                                            "Введіть нове ім'я:", QLineEdit::Normal, oldName, &ok);

    // Якщо користувач натиснув "Скасувати", залишив поле порожнім, або ввів те саме ім'я
    if (!ok || newName.isEmpty() || newName == oldName) {
        return;
    }

    // 4. Викликаємо метод ядра для перейменування
    if (vfs.renameNode(oldName.toStdString(), current_dir_index, newName.toStdString())) {
        refreshUI(); // Оновлюємо список на екрані
    } else {
        QMessageBox::critical(this, "Помилка", "Не вдалося перейменувати. Можливо, таке ім'я вже існує або у вас немає прав доступу.");
    }
}

// Допоміжна функція для запиту папки призначення (щоб не дублювати код)
int32_t getDestinationFolder(QWidget* parent, VirtualFileSystem& vfs, bool& ok) {
    QString destDirName = QInputDialog::getText(parent, "Папка призначення",
        "Введіть назву папки (або залишіть порожнім для кореня диска):", QLineEdit::Normal, "", &ok);

    if (!ok) return -1; // Натиснули скасувати

    if (destDirName.isEmpty()) {
        return -1; // Корінь диска
    } else {
        // Шукаємо індекс введеної папки в корені (-1)
        int32_t dest_id = vfs.getNodeIndex(destDirName.toStdString(), -1);
        if (dest_id == -1) {
            QMessageBox::warning(parent, "Помилка", "Таку папку не знайдено!");
        }
        return dest_id;
    }
}

// Обробка копіювання
void MainWindow::on_btnCopy_clicked() {
    QListWidgetItem *item = ui->fileListWidget->currentItem();
    if (!item || item->text() == "📁 .. (Назад)" || item->text().startsWith("📁 ")) {
        QMessageBox::warning(this, "Увага", "Оберіть ФАЙЛ для копіювання (папки копіювати не можна).");
        return;
    }

    QString srcName = item->text().mid(3).split(" (").first();

    bool ok1, ok2;
    QString destName = QInputDialog::getText(this, "Копіювання", "Введіть нове ім'я для копії:", QLineEdit::Normal, srcName + "_copy", &ok1);
    if (!ok1 || destName.isEmpty()) return;

    int32_t destParent = getDestinationFolder(this, vfs, ok2);
    if (!ok2 || destParent == -1 && !destName.isEmpty() && vfs.getNodeIndex(destName.toStdString(), -1) == -1 && destParent == -1) {
        // Якщо getDestinationFolder повернув помилку
    }

    if (ok2 && vfs.copyFile(srcName.toStdString(), current_dir_index, destName.toStdString(), destParent)) {
        refreshUI();
        QMessageBox::information(this, "Успіх", "Файл успішно скопійовано.");
    } else if (ok2) {
        QMessageBox::critical(this, "Помилка", "Не вдалося скопіювати. Конфлікт імен або відсутність доступу.");
    }
}

// Обробка переміщення
void MainWindow::on_btnMove_clicked() {
    QListWidgetItem *item = ui->fileListWidget->currentItem();
    if (!item || item->text() == "📁 .. (Назад)") {
        QMessageBox::warning(this, "Увага", "Оберіть файл або папку для переміщення.");
        return;
    }

    QString name = item->text().mid(3).split(" (").first();

    bool ok;
    int32_t destParent = getDestinationFolder(this, vfs, ok);

    if (ok && destParent != -2) { // -2 це наш умовний код помилки, якщо папка не знайдена
        if (destParent == current_dir_index) {
            QMessageBox::information(this, "Увага", "Файл вже знаходиться у цій папці.");
            return;
        }

        if (vfs.moveNode(name.toStdString(), current_dir_index, destParent)) {
            refreshUI();
        } else {
            QMessageBox::critical(this, "Помилка", "Не вдалося перемістити об'єкт.");
        }
    }
}
