#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "virtualfilesystem.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Слоти для кнопок (генеруються автоматично, якщо в UI натиснути "Go to slot")
    void on_btnFormat_clicked();
    void on_btnCreateDir_clicked();
    void on_btnCreateFile_clicked();
    void on_btnDelete_clicked();
    void on_btnCreateUser_clicked();
    void on_btnChangeUser_clicked();
    void on_btnRename_clicked();
    void on_btnCopy_clicked();
    void on_btnMove_clicked();

    // Слот для подвійного кліку по списку (для навігації в папки)
    void on_fileListWidget_doubleClicked(const QModelIndex &index);

private:
    Ui::MainWindow *ui;

    // Наш об'єкт файлової системи
    VirtualFileSystem vfs;

    // Індекс поточної папки (-1 означає корінь диска)
    int32_t current_dir_index;

    // Допоміжна функція для оновлення інтерфейсу
    void refreshUI();
};

#endif // MAINWINDOW_H
