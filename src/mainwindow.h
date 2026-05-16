#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "filetransferserver.h"

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QProgressBar;
class QTextEdit;
class QThread;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void chooseSaveDirectory();
    void startServer();
    void stopServer();
    void addFiles();
    void addDirectory();
    void removeSelectedItems();
    void clearItems();
    void sendSelection();
    void appendLog(const QString &message);
    void updateServerState(bool listening, const QString &message);
    void updateCurrentProgress(const QString &path, qint64 value, qint64 maximum);
    void updateTotalProgress(qint64 value, qint64 maximum);
    void transferFinished(bool ok, const QString &message);

private:
    void buildUi();
    void updateButtons();
    void addPathItem(const QString &path, const QString &type);
    QStringList selectedPaths(const QString &type) const;
    QString localAddressText() const;
    void setProgress(QProgressBar *bar, qint64 value, qint64 maximum);

    FileTransferServer *m_server;
    QThread *m_transferThread;

    QLineEdit *m_portEdit;
    QLineEdit *m_saveDirEdit;
    QLabel *m_serverStateLabel;
    QLabel *m_localAddressLabel;
    QPushButton *m_startServerButton;
    QPushButton *m_stopServerButton;

    QLineEdit *m_hostEdit;
    QLineEdit *m_remotePortEdit;
    QListWidget *m_pathList;
    QPushButton *m_sendButton;

    QLabel *m_currentFileLabel;
    QProgressBar *m_currentProgress;
    QProgressBar *m_totalProgress;
    QTextEdit *m_logEdit;
};

#endif // MAINWINDOW_H
