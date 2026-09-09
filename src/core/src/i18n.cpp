#include "winchisel/core/i18n.hpp"

#include <atomic>
#include <unordered_map>

namespace winchisel::core {
namespace {

// Atomic so background workers (log lines, progress callbacks) can read the
// language while the UI thread switches it without a data race.
std::atomic<Language> g_language{Language::english};

std::unordered_map<std::wstring, std::wstring> const& german() {
    static const std::unordered_map<std::wstring, std::wstring> table{
        {L"Home", L"Start"},
        {L"Debloater", L"Debloater"},
        {L"Performance", L"Leistung"},
        {L"Privacy & Security", L"Datenschutz & Sicherheit"},
        {L"Downloads", L"Downloads"},
        {L"Processes", L"Prozesse"},
        {L"Latency", L"Latenz"},
        {L"Extras", L"Extras"},
        {L"Settings", L"Einstellungen"},
        {L"Hardware", L"Hardware"},
        {L"System", L"System"},
        {L"Processor", L"Prozessor"},
        {L"Graphics", L"Grafik"},
        {L"Memory", L"Arbeitsspeicher"},
        {L"Storage", L"Speicher"},
        {L"Windows", L"Windows"},
        {L"Motherboard", L"Mainboard"},
        {L"Display", L"Anzeige"},
        {L"Uptime", L"Laufzeit"},
        {L"Keep the app behavior aligned with your workflow.", L"Passe das App-Verhalten an deinen Ablauf an."},
        {L"Application", L"Anwendung"},
        {L"These settings are saved automatically.", L"Diese Einstellungen werden automatisch gespeichert."},
        {L"Language", L"Sprache"},
        {L"Choose the application language.", L"Wähle die Sprache der Anwendung."},
        {L"Check updates on startup", L"Beim Start nach Updates suchen"},
        {L"Look for a newer Winchisel release when the app starts.", L"Beim Start nach einer neueren Winchisel-Version suchen."},
        {L"Show console", L"Konsole anzeigen"},
        {L"Show the debug console window while the app is running.", L"Debug-Konsole anzeigen, während die App läuft."},
        {L"Start with Windows", L"Mit Windows starten"},
        {L"Launch Winchisel automatically when you sign in.", L"Winchisel automatisch nach der Anmeldung starten."},
        {L"System Protection", L"Systemschutz"},
        {L"System Restore Point", L"Systemwiederherstellungspunkt"},
        {L"Remove or restore Windows apps, capabilities, and optional features.", L"Windows-Apps, Funktionen und optionale Features entfernen oder wiederherstellen."},
        {L"Windows Apps", L"Windows-Apps"},
        {L"Capabilities", L"Funktionen"},
        {L"Optional Features", L"Optionale Features"},
        {L"Search", L"Suche"},
        {L"Find and install trusted applications with winget.", L"Vertrauenswürdige Anwendungen mit winget finden und installieren."},
        {L"Search apps, categories, or package IDs", L"Apps, Kategorien oder Paket-IDs suchen"},
        {L"Inspect running processes, priority, and affinity.", L"Laufende Prozesse, Priorität und Affinität prüfen."},
        {L"Analyze USB topology for latency issues.", L"USB-Topologie auf Latenzprobleme analysieren."},
        {L"Ready", L"Bereit"},
        {L"Additional system, browser, networking, and power options.", L"Weitere System-, Browser-, Netzwerk- und Energieoptionen."},
        {L"Installed", L"Installiert"},
        {L"Not installed", L"Nicht installiert"},
        {L"Website", L"Website"},
        {L"Install", L"Installieren"},
        {L"Scan failed", L"Scan fehlgeschlagen"},
        {L"Installation complete", L"Installation abgeschlossen"},
        {L"Update", L"Aktualisieren"},
        {L"Later", L"Später"},
        {L"Winchisel update available", L"Winchisel-Update verfügbar"},
        {L"Open Store", L"Store öffnen"},
        {L"This Store version updates through the Microsoft Store. Open it now to install the update?", L"Diese Store-Version wird über den Microsoft Store aktualisiert. Store jetzt zum Aktualisieren öffnen?"},
        {L"Create Restore Point", L"Wiederherstellungspunkt erstellen"},
        {L"System Repair", L"Systemreparatur"},
        {L"Temporary Files - Remove", L"Temporäre Dateien entfernen"},
        {L"Close", L"Schließen"},
        {L"Cancel", L"Abbrechen"},
        {L"Apply", L"Übernehmen"},
        {L"Live log", L"Live-Protokoll"},
        {L"Waiting for output...", L"Warte auf Ausgabe..."},
        {L"This can take a while. Keep the window open until it finishes.", L"Das kann eine Weile dauern. Lass das Fenster geöffnet, bis es fertig ist."},
        {L"Could not apply setting", L"Einstellung konnte nicht übernommen werden"},
        {L"Recommended", L"Empfohlen"},
        {L"Defaults", L"Standard"},
        {L"Settings could not be saved", L"Einstellungen konnten nicht gespeichert werden"},
        {L"Refresh", L"Aktualisieren"},
        {L"New", L"Neu"},
        {L"Startup", L"Autostart"},
        {L"Search startup entries", L"Autostart-Einträge suchen"},
        {L"Loading startup entries...", L"Autostart-Einträge werden geladen..."},
        {L"startup entries", L"Autostart-Einträge"},
        {L"Registry", L"Registrierung"},
        {L"Folder", L"Ordner"},
        {L"Scheduled task", L"Geplante Aufgabe"},
        {L"All", L"Alle"},
        {L"Active", L"Aktiv"},
        {L"User", L"Benutzer"},
    };
    return table;
}

std::unordered_map<std::wstring, std::wstring> const& spanish() {
    static const std::unordered_map<std::wstring, std::wstring> table{
        {L"Home", L"Inicio"}, {L"Debloater", L"Optimizador"}, {L"Performance", L"Rendimiento"},
        {L"Privacy & Security", L"Privacidad y seguridad"}, {L"Downloads", L"Descargas"},
        {L"Processes", L"Procesos"}, {L"Latency", L"Latencia"}, {L"Extras", L"Extras"},
        {L"Settings", L"Configuración"}, {L"Hardware", L"Hardware"}, {L"System", L"Sistema"},
        {L"Processor", L"Procesador"}, {L"Graphics", L"Gráficos"}, {L"Memory", L"Memoria"},
        {L"Storage", L"Almacenamiento"}, {L"Windows", L"Windows"}, {L"Motherboard", L"Placa base"},
        {L"Display", L"Pantalla"}, {L"Uptime", L"Tiempo activo"},
        {L"Keep the app behavior aligned with your workflow.", L"Adapta el comportamiento de la aplicación a tu flujo de trabajo."},
        {L"Application", L"Aplicación"}, {L"These settings are saved automatically.", L"Estos ajustes se guardan automáticamente."},
        {L"Language", L"Idioma"}, {L"Choose the application language.", L"Elige el idioma de la aplicación."},
        {L"Check updates on startup", L"Buscar actualizaciones al iniciar"},
        {L"Look for a newer Winchisel release when the app starts.", L"Buscar una versión más reciente de Winchisel al iniciar la aplicación."},
        {L"Show console", L"Mostrar consola"}, {L"Show the debug console window while the app is running.", L"Mostrar la consola de depuración mientras se ejecuta la aplicación."},
        {L"Start with Windows", L"Iniciar con Windows"}, {L"Launch Winchisel automatically when you sign in.", L"Iniciar Winchisel automáticamente al iniciar sesión."},
        {L"System Protection", L"Protección del sistema"}, {L"System Restore Point", L"Punto de restauración del sistema"},
        {L"Remove or restore Windows apps, capabilities, and optional features.", L"Elimina o restaura aplicaciones, capacidades y características opcionales de Windows."},
        {L"Windows Apps", L"Aplicaciones de Windows"}, {L"Capabilities", L"Capacidades"}, {L"Optional Features", L"Características opcionales"},
        {L"Search", L"Buscar"}, {L"Find and install trusted applications with winget.", L"Busca e instala aplicaciones de confianza con winget."},
        {L"Search apps, categories, or package IDs", L"Buscar aplicaciones, categorías o identificadores de paquetes"},
        {L"Inspect running processes, priority, and affinity.", L"Inspecciona los procesos en ejecución, la prioridad y la afinidad."},
        {L"Analyze USB topology for latency issues.", L"Analiza la topología USB para detectar problemas de latencia."},
        {L"Ready", L"Listo"}, {L"Additional system, browser, networking, and power options.", L"Opciones adicionales del sistema, navegador, red y energía."},
        {L"Installed", L"Instalado"}, {L"Not installed", L"No instalado"}, {L"Website", L"Sitio web"},
        {L"Install", L"Instalar"}, {L"Scan failed", L"Error de análisis"}, {L"Installation complete", L"Instalación completada"},
        {L"Update", L"Actualizar"}, {L"Later", L"Más tarde"}, {L"Winchisel update available", L"Actualización de Winchisel disponible"},
        {L"Create Restore Point", L"Crear punto de restauración"}, {L"System Repair", L"Reparación del sistema"},
        {L"Temporary Files - Remove", L"Eliminar archivos temporales"}, {L"Close", L"Cerrar"}, {L"Cancel", L"Cancelar"},
        {L"Apply", L"Aplicar"}, {L"Live log", L"Registro en directo"}, {L"Waiting for output...", L"Esperando resultados..."},
        {L"This can take a while. Keep the window open until it finishes.", L"Esto puede tardar. Mantén la ventana abierta hasta que termine."},
        {L"Could not apply setting", L"No se pudo aplicar el ajuste"}, {L"Recommended", L"Recomendado"},
        {L"Defaults", L"Valores predeterminados"}, {L"Settings could not be saved", L"No se pudieron guardar los ajustes"},
        {L"Refresh", L"Actualizar"}, {L"All", L"Todos"}, {L"Active", L"Activos"}, {L"User", L"Usuario"},
    };
    return table;
}

std::unordered_map<std::wstring, std::wstring> const& french() {
    static const std::unordered_map<std::wstring, std::wstring> table{
        {L"Home", L"Accueil"}, {L"Debloater", L"Optimisation"}, {L"Performance", L"Performances"},
        {L"Privacy & Security", L"Confidentialité et sécurité"}, {L"Downloads", L"Téléchargements"},
        {L"Processes", L"Processus"}, {L"Latency", L"Latence"}, {L"Extras", L"Extras"},
        {L"Settings", L"Paramètres"}, {L"Hardware", L"Matériel"}, {L"System", L"Système"},
        {L"Processor", L"Processeur"}, {L"Graphics", L"Graphiques"}, {L"Memory", L"Mémoire"},
        {L"Storage", L"Stockage"}, {L"Windows", L"Windows"}, {L"Motherboard", L"Carte mère"},
        {L"Display", L"Écran"}, {L"Uptime", L"Durée de fonctionnement"},
        {L"Keep the app behavior aligned with your workflow.", L"Adaptez le comportement de l’application à votre flux de travail."},
        {L"Application", L"Application"}, {L"These settings are saved automatically.", L"Ces paramètres sont enregistrés automatiquement."},
        {L"Language", L"Langue"}, {L"Choose the application language.", L"Choisissez la langue de l’application."},
        {L"Check updates on startup", L"Rechercher les mises à jour au démarrage"},
        {L"Look for a newer Winchisel release when the app starts.", L"Rechercher une version plus récente de Winchisel au démarrage."},
        {L"Show console", L"Afficher la console"}, {L"Show the debug console window while the app is running.", L"Afficher la console de débogage pendant l’exécution de l’application."},
        {L"Start with Windows", L"Démarrer avec Windows"}, {L"Launch Winchisel automatically when you sign in.", L"Lancer Winchisel automatiquement à la connexion."},
        {L"System Protection", L"Protection du système"}, {L"System Restore Point", L"Point de restauration système"},
        {L"Remove or restore Windows apps, capabilities, and optional features.", L"Supprimez ou restaurez les applications, capacités et fonctionnalités facultatives de Windows."},
        {L"Windows Apps", L"Applications Windows"}, {L"Capabilities", L"Capacités"}, {L"Optional Features", L"Fonctionnalités facultatives"},
        {L"Search", L"Rechercher"}, {L"Find and install trusted applications with winget.", L"Recherchez et installez des applications fiables avec winget."},
        {L"Search apps, categories, or package IDs", L"Rechercher des applications, catégories ou identifiants de paquet"},
        {L"Inspect running processes, priority, and affinity.", L"Inspectez les processus en cours, leur priorité et leur affinité."},
        {L"Analyze USB topology for latency issues.", L"Analysez la topologie USB pour détecter les problèmes de latence."},
        {L"Ready", L"Prêt"}, {L"Additional system, browser, networking, and power options.", L"Options supplémentaires pour le système, le navigateur, le réseau et l’alimentation."},
        {L"Installed", L"Installé"}, {L"Not installed", L"Non installé"}, {L"Website", L"Site web"},
        {L"Install", L"Installer"}, {L"Scan failed", L"Échec de l’analyse"}, {L"Installation complete", L"Installation terminée"},
        {L"Update", L"Mettre à jour"}, {L"Later", L"Plus tard"}, {L"Winchisel update available", L"Mise à jour de Winchisel disponible"},
        {L"Create Restore Point", L"Créer un point de restauration"}, {L"System Repair", L"Réparation du système"},
        {L"Temporary Files - Remove", L"Supprimer les fichiers temporaires"}, {L"Close", L"Fermer"}, {L"Cancel", L"Annuler"},
        {L"Apply", L"Appliquer"}, {L"Live log", L"Journal en direct"}, {L"Waiting for output...", L"En attente des résultats..."},
        {L"This can take a while. Keep the window open until it finishes.", L"Cette opération peut prendre du temps. Gardez la fenêtre ouverte jusqu’à la fin."},
        {L"Could not apply setting", L"Impossible d’appliquer le paramètre"}, {L"Recommended", L"Recommandé"},
        {L"Defaults", L"Valeurs par défaut"}, {L"Settings could not be saved", L"Impossible d’enregistrer les paramètres"},
        {L"Refresh", L"Actualiser"}, {L"All", L"Tous"}, {L"Active", L"Actifs"}, {L"User", L"Utilisateur"},
    };
    return table;
}

std::unordered_map<std::wstring, std::wstring> const& russian() {
    static const std::unordered_map<std::wstring, std::wstring> table{
        {L"Home",L"Главная"},{L"Debloater",L"Оптимизация"},{L"Performance",L"Производительность"},{L"Privacy & Security",L"Конфиденциальность и безопасность"},
        {L"Downloads",L"Загрузки"},{L"Processes",L"Процессы"},{L"Latency",L"Задержка"},{L"Extras",L"Дополнительно"},{L"Settings",L"Настройки"},
        {L"Hardware",L"Оборудование"},{L"System",L"Система"},{L"Processor",L"Процессор"},{L"Graphics",L"Графика"},{L"Memory",L"Память"},
        {L"Storage",L"Хранилище"},{L"Windows",L"Windows"},{L"Motherboard",L"Материнская плата"},{L"Display",L"Экран"},{L"Uptime",L"Время работы"},
        {L"Keep the app behavior aligned with your workflow.",L"Настройте приложение под свой рабочий процесс."},{L"Application",L"Приложение"},
        {L"These settings are saved automatically.",L"Эти настройки сохраняются автоматически."},{L"Language",L"Язык"},{L"Choose the application language.",L"Выберите язык приложения."},
        {L"Check updates on startup",L"Проверять обновления при запуске"},{L"Look for a newer Winchisel release when the app starts.",L"Искать новую версию Winchisel при запуске приложения."},
        {L"Show console",L"Показывать консоль"},{L"Show the debug console window while the app is running.",L"Показывать консоль отладки во время работы приложения."},
        {L"Start with Windows",L"Запускать вместе с Windows"},{L"Launch Winchisel automatically when you sign in.",L"Автоматически запускать Winchisel при входе в систему."},
        {L"System Protection",L"Защита системы"},{L"System Restore Point",L"Точка восстановления системы"},
        {L"Remove or restore Windows apps, capabilities, and optional features.",L"Удаляйте или восстанавливайте приложения, компоненты и дополнительные функции Windows."},
        {L"Windows Apps",L"Приложения Windows"},{L"Capabilities",L"Компоненты"},{L"Optional Features",L"Дополнительные функции"},{L"Search",L"Поиск"},
        {L"Find and install trusted applications with winget.",L"Находите и устанавливайте проверенные приложения с помощью winget."},
        {L"Search apps, categories, or package IDs",L"Поиск приложений, категорий или идентификаторов пакетов"},
        {L"Inspect running processes, priority, and affinity.",L"Просматривайте запущенные процессы, их приоритет и привязку."},
        {L"Analyze USB topology for latency issues.",L"Анализируйте топологию USB на наличие проблем с задержкой."},{L"Ready",L"Готово"},
        {L"Additional system, browser, networking, and power options.",L"Дополнительные параметры системы, браузера, сети и питания."},
        {L"Installed",L"Установлено"},{L"Not installed",L"Не установлено"},{L"Website",L"Веб-сайт"},{L"Install",L"Установить"},{L"Scan failed",L"Ошибка сканирования"},
        {L"Installation complete",L"Установка завершена"},{L"Update",L"Обновить"},{L"Later",L"Позже"},{L"Winchisel update available",L"Доступно обновление Winchisel"},
        {L"Create Restore Point",L"Создать точку восстановления"},{L"System Repair",L"Восстановление системы"},{L"Temporary Files - Remove",L"Удалить временные файлы"},
        {L"Close",L"Закрыть"},{L"Cancel",L"Отмена"},{L"Apply",L"Применить"},{L"Live log",L"Журнал в реальном времени"},{L"Waiting for output...",L"Ожидание результата..."},
        {L"This can take a while. Keep the window open until it finishes.",L"Это может занять некоторое время. Не закрывайте окно до завершения."},
        {L"Could not apply setting",L"Не удалось применить настройку"},{L"Recommended",L"Рекомендуемые"},{L"Defaults",L"По умолчанию"},
        {L"Settings could not be saved",L"Не удалось сохранить настройки"},{L"Refresh",L"Обновить"},{L"All",L"Все"},{L"Active",L"Активные"},{L"User",L"Пользователь"},
    }; return table;
}

std::unordered_map<std::wstring, std::wstring> const& simplified_chinese() {
    static const std::unordered_map<std::wstring, std::wstring> table{
        {L"Home",L"主页"},{L"Debloater",L"精简优化"},{L"Performance",L"性能"},{L"Privacy & Security",L"隐私与安全"},{L"Downloads",L"下载"},
        {L"Processes",L"进程"},{L"Latency",L"延迟"},{L"Extras",L"其他"},{L"Settings",L"设置"},{L"Hardware",L"硬件"},{L"System",L"系统"},
        {L"Processor",L"处理器"},{L"Graphics",L"显卡"},{L"Memory",L"内存"},{L"Storage",L"存储"},{L"Windows",L"Windows"},{L"Motherboard",L"主板"},
        {L"Display",L"显示器"},{L"Uptime",L"运行时间"},{L"Keep the app behavior aligned with your workflow.",L"使应用行为符合您的工作流程。"},
        {L"Application",L"应用"},{L"These settings are saved automatically.",L"这些设置会自动保存。"},{L"Language",L"语言"},{L"Choose the application language.",L"选择应用语言。"},
        {L"Check updates on startup",L"启动时检查更新"},{L"Look for a newer Winchisel release when the app starts.",L"应用启动时检查较新的 Winchisel 版本。"},
        {L"Show console",L"显示控制台"},{L"Show the debug console window while the app is running.",L"应用运行时显示调试控制台窗口。"},
        {L"Start with Windows",L"随 Windows 启动"},{L"Launch Winchisel automatically when you sign in.",L"登录时自动启动 Winchisel。"},
        {L"System Protection",L"系统保护"},{L"System Restore Point",L"系统还原点"},{L"Remove or restore Windows apps, capabilities, and optional features.",L"移除或恢复 Windows 应用、功能和可选功能。"},
        {L"Windows Apps",L"Windows 应用"},{L"Capabilities",L"功能"},{L"Optional Features",L"可选功能"},{L"Search",L"搜索"},
        {L"Find and install trusted applications with winget.",L"使用 winget 查找并安装可信应用。"},{L"Search apps, categories, or package IDs",L"搜索应用、类别或软件包 ID"},
        {L"Inspect running processes, priority, and affinity.",L"查看正在运行的进程、优先级和处理器关联。"},{L"Analyze USB topology for latency issues.",L"分析 USB 拓扑中的延迟问题。"},
        {L"Ready",L"就绪"},{L"Additional system, browser, networking, and power options.",L"其他系统、浏览器、网络和电源选项。"},
        {L"Installed",L"已安装"},{L"Not installed",L"未安装"},{L"Website",L"网站"},{L"Install",L"安装"},{L"Scan failed",L"扫描失败"},{L"Installation complete",L"安装完成"},
        {L"Update",L"更新"},{L"Later",L"稍后"},{L"Winchisel update available",L"Winchisel 有可用更新"},{L"Create Restore Point",L"创建还原点"},
        {L"System Repair",L"系统修复"},{L"Temporary Files - Remove",L"删除临时文件"},{L"Close",L"关闭"},{L"Cancel",L"取消"},{L"Apply",L"应用"},
        {L"Live log",L"实时日志"},{L"Waiting for output...",L"正在等待输出..."},{L"This can take a while. Keep the window open until it finishes.",L"这可能需要一些时间。请保持窗口打开直至完成。"},
        {L"Could not apply setting",L"无法应用设置"},{L"Recommended",L"推荐"},{L"Defaults",L"默认值"},{L"Settings could not be saved",L"无法保存设置"},
        {L"Refresh",L"刷新"},{L"All",L"全部"},{L"Active",L"活动"},{L"User",L"用户"},
    }; return table;
}

using Table = std::unordered_map<std::wstring, std::wstring>;

Table const& portuguese_brazil() {
    static const Table t{{L"Home",L"Início"},{L"Debloater",L"Otimização"},{L"Performance",L"Desempenho"},{L"Privacy & Security",L"Privacidade e segurança"},{L"Downloads",L"Downloads"},{L"Processes",L"Processos"},{L"Latency",L"Latência"},{L"Extras",L"Extras"},{L"Settings",L"Configurações"},{L"System",L"Sistema"},{L"Processor",L"Processador"},{L"Graphics",L"Gráficos"},{L"Memory",L"Memória"},{L"Storage",L"Armazenamento"},{L"Motherboard",L"Placa-mãe"},{L"Display",L"Tela"},{L"Uptime",L"Tempo de atividade"},{L"Application",L"Aplicativo"},{L"Language",L"Idioma"},{L"Choose the application language.",L"Escolha o idioma do aplicativo."},{L"These settings are saved automatically.",L"Estas configurações são salvas automaticamente."},{L"Check updates on startup",L"Verificar atualizações ao iniciar"},{L"Show console",L"Mostrar console"},{L"Start with Windows",L"Iniciar com o Windows"},{L"System Protection",L"Proteção do sistema"},{L"System Restore Point",L"Ponto de restauração do sistema"},{L"Windows Apps",L"Aplicativos do Windows"},{L"Capabilities",L"Recursos"},{L"Optional Features",L"Recursos opcionais"},{L"Search",L"Pesquisar"},{L"Installed",L"Instalado"},{L"Not installed",L"Não instalado"},{L"Install",L"Instalar"},{L"Update",L"Atualizar"},{L"Later",L"Mais tarde"},{L"Close",L"Fechar"},{L"Cancel",L"Cancelar"},{L"Apply",L"Aplicar"},{L"Recommended",L"Recomendado"},{L"Defaults",L"Padrões"},{L"Refresh",L"Atualizar"},{L"All",L"Todos"},{L"Active",L"Ativos"},{L"User",L"Usuário"},{L"Ready",L"Pronto"}}; return t;
}
Table const& polish() {
    static const Table t{{L"Home",L"Strona główna"},{L"Debloater",L"Optymalizacja"},{L"Performance",L"Wydajność"},{L"Privacy & Security",L"Prywatność i zabezpieczenia"},{L"Downloads",L"Pobieranie"},{L"Processes",L"Procesy"},{L"Latency",L"Opóźnienia"},{L"Extras",L"Dodatki"},{L"Settings",L"Ustawienia"},{L"System",L"System"},{L"Processor",L"Procesor"},{L"Graphics",L"Grafika"},{L"Memory",L"Pamięć"},{L"Storage",L"Pamięć masowa"},{L"Motherboard",L"Płyta główna"},{L"Display",L"Ekran"},{L"Uptime",L"Czas pracy"},{L"Application",L"Aplikacja"},{L"Language",L"Język"},{L"Choose the application language.",L"Wybierz język aplikacji."},{L"These settings are saved automatically.",L"Te ustawienia są zapisywane automatycznie."},{L"Check updates on startup",L"Sprawdzaj aktualizacje przy uruchomieniu"},{L"Show console",L"Pokaż konsolę"},{L"Start with Windows",L"Uruchamiaj z systemem Windows"},{L"System Protection",L"Ochrona systemu"},{L"System Restore Point",L"Punkt przywracania systemu"},{L"Windows Apps",L"Aplikacje Windows"},{L"Capabilities",L"Możliwości"},{L"Optional Features",L"Funkcje opcjonalne"},{L"Search",L"Szukaj"},{L"Installed",L"Zainstalowane"},{L"Not installed",L"Niezainstalowane"},{L"Install",L"Zainstaluj"},{L"Update",L"Aktualizuj"},{L"Later",L"Później"},{L"Close",L"Zamknij"},{L"Cancel",L"Anuluj"},{L"Apply",L"Zastosuj"},{L"Recommended",L"Zalecane"},{L"Defaults",L"Domyślne"},{L"Refresh",L"Odśwież"},{L"All",L"Wszystkie"},{L"Active",L"Aktywne"},{L"User",L"Użytkownik"},{L"Ready",L"Gotowe"}}; return t;
}
Table const& turkish() {
    static const Table t{{L"Home",L"Ana Sayfa"},{L"Debloater",L"Optimizasyon"},{L"Performance",L"Performans"},{L"Privacy & Security",L"Gizlilik ve Güvenlik"},{L"Downloads",L"İndirilenler"},{L"Processes",L"İşlemler"},{L"Latency",L"Gecikme"},{L"Extras",L"Ekstralar"},{L"Settings",L"Ayarlar"},{L"System",L"Sistem"},{L"Processor",L"İşlemci"},{L"Graphics",L"Grafik"},{L"Memory",L"Bellek"},{L"Storage",L"Depolama"},{L"Motherboard",L"Anakart"},{L"Display",L"Ekran"},{L"Uptime",L"Çalışma süresi"},{L"Application",L"Uygulama"},{L"Language",L"Dil"},{L"Choose the application language.",L"Uygulama dilini seçin."},{L"These settings are saved automatically.",L"Bu ayarlar otomatik olarak kaydedilir."},{L"Check updates on startup",L"Başlangıçta güncellemeleri denetle"},{L"Show console",L"Konsolu göster"},{L"Start with Windows",L"Windows ile başlat"},{L"System Protection",L"Sistem Koruması"},{L"System Restore Point",L"Sistem Geri Yükleme Noktası"},{L"Windows Apps",L"Windows Uygulamaları"},{L"Capabilities",L"Yetenekler"},{L"Optional Features",L"İsteğe Bağlı Özellikler"},{L"Search",L"Ara"},{L"Installed",L"Yüklü"},{L"Not installed",L"Yüklü değil"},{L"Install",L"Yükle"},{L"Update",L"Güncelle"},{L"Later",L"Daha sonra"},{L"Close",L"Kapat"},{L"Cancel",L"İptal"},{L"Apply",L"Uygula"},{L"Recommended",L"Önerilen"},{L"Defaults",L"Varsayılanlar"},{L"Refresh",L"Yenile"},{L"All",L"Tümü"},{L"Active",L"Etkin"},{L"User",L"Kullanıcı"},{L"Ready",L"Hazır"}}; return t;
}
Table const& japanese() {
    static const Table t{{L"Home",L"ホーム"},{L"Debloater",L"最適化"},{L"Performance",L"パフォーマンス"},{L"Privacy & Security",L"プライバシーとセキュリティ"},{L"Downloads",L"ダウンロード"},{L"Processes",L"プロセス"},{L"Latency",L"遅延"},{L"Extras",L"その他"},{L"Settings",L"設定"},{L"System",L"システム"},{L"Processor",L"プロセッサ"},{L"Graphics",L"グラフィックス"},{L"Memory",L"メモリ"},{L"Storage",L"ストレージ"},{L"Motherboard",L"マザーボード"},{L"Display",L"ディスプレイ"},{L"Uptime",L"稼働時間"},{L"Application",L"アプリケーション"},{L"Language",L"言語"},{L"Choose the application language.",L"アプリケーションの言語を選択します。"},{L"These settings are saved automatically.",L"これらの設定は自動的に保存されます。"},{L"Check updates on startup",L"起動時に更新を確認"},{L"Show console",L"コンソールを表示"},{L"Start with Windows",L"Windows と同時に起動"},{L"System Protection",L"システムの保護"},{L"System Restore Point",L"システムの復元ポイント"},{L"Windows Apps",L"Windows アプリ"},{L"Capabilities",L"機能"},{L"Optional Features",L"オプション機能"},{L"Search",L"検索"},{L"Installed",L"インストール済み"},{L"Not installed",L"未インストール"},{L"Install",L"インストール"},{L"Update",L"更新"},{L"Later",L"後で"},{L"Close",L"閉じる"},{L"Cancel",L"キャンセル"},{L"Apply",L"適用"},{L"Recommended",L"推奨"},{L"Defaults",L"既定値"},{L"Refresh",L"更新"},{L"All",L"すべて"},{L"Active",L"アクティブ"},{L"User",L"ユーザー"},{L"Ready",L"準備完了"}}; return t;
}
Table const& korean() {
    static const Table t{{L"Home",L"홈"},{L"Debloater",L"최적화"},{L"Performance",L"성능"},{L"Privacy & Security",L"개인 정보 및 보안"},{L"Downloads",L"다운로드"},{L"Processes",L"프로세스"},{L"Latency",L"지연 시간"},{L"Extras",L"기타"},{L"Settings",L"설정"},{L"System",L"시스템"},{L"Processor",L"프로세서"},{L"Graphics",L"그래픽"},{L"Memory",L"메모리"},{L"Storage",L"저장소"},{L"Motherboard",L"메인보드"},{L"Display",L"디스플레이"},{L"Uptime",L"가동 시간"},{L"Application",L"애플리케이션"},{L"Language",L"언어"},{L"Choose the application language.",L"애플리케이션 언어를 선택하세요."},{L"These settings are saved automatically.",L"이 설정은 자동으로 저장됩니다."},{L"Check updates on startup",L"시작할 때 업데이트 확인"},{L"Show console",L"콘솔 표시"},{L"Start with Windows",L"Windows와 함께 시작"},{L"System Protection",L"시스템 보호"},{L"System Restore Point",L"시스템 복원 지점"},{L"Windows Apps",L"Windows 앱"},{L"Capabilities",L"기능"},{L"Optional Features",L"선택적 기능"},{L"Search",L"검색"},{L"Installed",L"설치됨"},{L"Not installed",L"설치되지 않음"},{L"Install",L"설치"},{L"Update",L"업데이트"},{L"Later",L"나중에"},{L"Close",L"닫기"},{L"Cancel",L"취소"},{L"Apply",L"적용"},{L"Recommended",L"권장"},{L"Defaults",L"기본값"},{L"Refresh",L"새로 고침"},{L"All",L"모두"},{L"Active",L"활성"},{L"User",L"사용자"},{L"Ready",L"준비됨"}}; return t;
}
Table const& italian() {
    static const Table t{{L"Home",L"Home"},{L"Debloater",L"Ottimizzazione"},{L"Performance",L"Prestazioni"},{L"Privacy & Security",L"Privacy e sicurezza"},{L"Downloads",L"Download"},{L"Processes",L"Processi"},{L"Latency",L"Latenza"},{L"Extras",L"Extra"},{L"Settings",L"Impostazioni"},{L"System",L"Sistema"},{L"Processor",L"Processore"},{L"Graphics",L"Grafica"},{L"Memory",L"Memoria"},{L"Storage",L"Archiviazione"},{L"Motherboard",L"Scheda madre"},{L"Display",L"Schermo"},{L"Uptime",L"Tempo di attività"},{L"Application",L"Applicazione"},{L"Language",L"Lingua"},{L"Choose the application language.",L"Scegli la lingua dell’applicazione."},{L"These settings are saved automatically.",L"Queste impostazioni vengono salvate automaticamente."},{L"Check updates on startup",L"Controlla aggiornamenti all’avvio"},{L"Show console",L"Mostra console"},{L"Start with Windows",L"Avvia con Windows"},{L"System Protection",L"Protezione sistema"},{L"System Restore Point",L"Punto di ripristino del sistema"},{L"Windows Apps",L"App Windows"},{L"Capabilities",L"Funzionalità"},{L"Optional Features",L"Funzionalità facoltative"},{L"Search",L"Cerca"},{L"Installed",L"Installato"},{L"Not installed",L"Non installato"},{L"Install",L"Installa"},{L"Update",L"Aggiorna"},{L"Later",L"Più tardi"},{L"Close",L"Chiudi"},{L"Cancel",L"Annulla"},{L"Apply",L"Applica"},{L"Recommended",L"Consigliati"},{L"Defaults",L"Predefiniti"},{L"Refresh",L"Aggiorna"},{L"All",L"Tutti"},{L"Active",L"Attivi"},{L"User",L"Utente"},{L"Ready",L"Pronto"}}; return t;
}
Table const& dutch() {
    static const Table t{{L"Home",L"Start"},{L"Debloater",L"Optimalisatie"},{L"Performance",L"Prestaties"},{L"Privacy & Security",L"Privacy en beveiliging"},{L"Downloads",L"Downloads"},{L"Processes",L"Processen"},{L"Latency",L"Latentie"},{L"Extras",L"Extra's"},{L"Settings",L"Instellingen"},{L"System",L"Systeem"},{L"Processor",L"Processor"},{L"Graphics",L"Grafische kaart"},{L"Memory",L"Geheugen"},{L"Storage",L"Opslag"},{L"Motherboard",L"Moederbord"},{L"Display",L"Beeldscherm"},{L"Uptime",L"Actieve tijd"},{L"Application",L"Toepassing"},{L"Language",L"Taal"},{L"Choose the application language.",L"Kies de taal van de toepassing."},{L"These settings are saved automatically.",L"Deze instellingen worden automatisch opgeslagen."},{L"Check updates on startup",L"Controleren op updates bij opstarten"},{L"Show console",L"Console weergeven"},{L"Start with Windows",L"Starten met Windows"},{L"System Protection",L"Systeembeveiliging"},{L"System Restore Point",L"Systeemherstelpunt"},{L"Windows Apps",L"Windows-apps"},{L"Capabilities",L"Mogelijkheden"},{L"Optional Features",L"Optionele onderdelen"},{L"Search",L"Zoeken"},{L"Installed",L"Geïnstalleerd"},{L"Not installed",L"Niet geïnstalleerd"},{L"Install",L"Installeren"},{L"Update",L"Bijwerken"},{L"Later",L"Later"},{L"Close",L"Sluiten"},{L"Cancel",L"Annuleren"},{L"Apply",L"Toepassen"},{L"Recommended",L"Aanbevolen"},{L"Defaults",L"Standaardwaarden"},{L"Refresh",L"Vernieuwen"},{L"All",L"Alle"},{L"Active",L"Actief"},{L"User",L"Gebruiker"},{L"Ready",L"Gereed"}}; return t;
}
Table const& ukrainian() {
    static const Table t{{L"Home",L"Головна"},{L"Debloater",L"Оптимізація"},{L"Performance",L"Продуктивність"},{L"Privacy & Security",L"Конфіденційність і безпека"},{L"Downloads",L"Завантаження"},{L"Processes",L"Процеси"},{L"Latency",L"Затримка"},{L"Extras",L"Додатково"},{L"Settings",L"Налаштування"},{L"System",L"Система"},{L"Processor",L"Процесор"},{L"Graphics",L"Графіка"},{L"Memory",L"Пам’ять"},{L"Storage",L"Сховище"},{L"Motherboard",L"Материнська плата"},{L"Display",L"Дисплей"},{L"Uptime",L"Час роботи"},{L"Application",L"Застосунок"},{L"Language",L"Мова"},{L"Choose the application language.",L"Виберіть мову застосунку."},{L"These settings are saved automatically.",L"Ці налаштування зберігаються автоматично."},{L"Check updates on startup",L"Перевіряти оновлення під час запуску"},{L"Show console",L"Показувати консоль"},{L"Start with Windows",L"Запускати разом із Windows"},{L"System Protection",L"Захист системи"},{L"System Restore Point",L"Точка відновлення системи"},{L"Windows Apps",L"Застосунки Windows"},{L"Capabilities",L"Можливості"},{L"Optional Features",L"Додаткові компоненти"},{L"Search",L"Пошук"},{L"Installed",L"Встановлено"},{L"Not installed",L"Не встановлено"},{L"Install",L"Встановити"},{L"Update",L"Оновити"},{L"Later",L"Пізніше"},{L"Close",L"Закрити"},{L"Cancel",L"Скасувати"},{L"Apply",L"Застосувати"},{L"Recommended",L"Рекомендовано"},{L"Defaults",L"Типові"},{L"Refresh",L"Оновити"},{L"All",L"Усі"},{L"Active",L"Активні"},{L"User",L"Користувач"},{L"Ready",L"Готово"}}; return t;
}
Table const& czech() {
    static const Table t{{L"Home",L"Domů"},{L"Debloater",L"Optimalizace"},{L"Performance",L"Výkon"},{L"Privacy & Security",L"Soukromí a zabezpečení"},{L"Downloads",L"Stahování"},{L"Processes",L"Procesy"},{L"Latency",L"Latence"},{L"Extras",L"Doplňky"},{L"Settings",L"Nastavení"},{L"System",L"Systém"},{L"Processor",L"Procesor"},{L"Graphics",L"Grafika"},{L"Memory",L"Paměť"},{L"Storage",L"Úložiště"},{L"Motherboard",L"Základní deska"},{L"Display",L"Displej"},{L"Uptime",L"Doba provozu"},{L"Application",L"Aplikace"},{L"Language",L"Jazyk"},{L"Choose the application language.",L"Vyberte jazyk aplikace."},{L"These settings are saved automatically.",L"Tato nastavení se ukládají automaticky."},{L"Check updates on startup",L"Kontrolovat aktualizace při spuštění"},{L"Show console",L"Zobrazit konzoli"},{L"Start with Windows",L"Spouštět se systémem Windows"},{L"System Protection",L"Ochrana systému"},{L"System Restore Point",L"Bod obnovení systému"},{L"Windows Apps",L"Aplikace Windows"},{L"Capabilities",L"Funkce"},{L"Optional Features",L"Volitelné funkce"},{L"Search",L"Hledat"},{L"Installed",L"Nainstalováno"},{L"Not installed",L"Nenainstalováno"},{L"Install",L"Nainstalovat"},{L"Update",L"Aktualizovat"},{L"Later",L"Později"},{L"Close",L"Zavřít"},{L"Cancel",L"Zrušit"},{L"Apply",L"Použít"},{L"Recommended",L"Doporučeno"},{L"Defaults",L"Výchozí"},{L"Refresh",L"Obnovit"},{L"All",L"Vše"},{L"Active",L"Aktivní"},{L"User",L"Uživatel"},{L"Ready",L"Připraveno"}}; return t;
}
Table const& indonesian() {
    static const Table t{{L"Home",L"Beranda"},{L"Debloater",L"Optimasi"},{L"Performance",L"Performa"},{L"Privacy & Security",L"Privasi & Keamanan"},{L"Downloads",L"Unduhan"},{L"Processes",L"Proses"},{L"Latency",L"Latensi"},{L"Extras",L"Lainnya"},{L"Settings",L"Pengaturan"},{L"System",L"Sistem"},{L"Processor",L"Prosesor"},{L"Graphics",L"Grafis"},{L"Memory",L"Memori"},{L"Storage",L"Penyimpanan"},{L"Motherboard",L"Papan induk"},{L"Display",L"Layar"},{L"Uptime",L"Waktu aktif"},{L"Application",L"Aplikasi"},{L"Language",L"Bahasa"},{L"Choose the application language.",L"Pilih bahasa aplikasi."},{L"These settings are saved automatically.",L"Pengaturan ini disimpan secara otomatis."},{L"Check updates on startup",L"Periksa pembaruan saat mulai"},{L"Show console",L"Tampilkan konsol"},{L"Start with Windows",L"Mulai bersama Windows"},{L"System Protection",L"Perlindungan Sistem"},{L"System Restore Point",L"Titik Pemulihan Sistem"},{L"Windows Apps",L"Aplikasi Windows"},{L"Capabilities",L"Kemampuan"},{L"Optional Features",L"Fitur Opsional"},{L"Search",L"Cari"},{L"Installed",L"Terpasang"},{L"Not installed",L"Belum terpasang"},{L"Install",L"Pasang"},{L"Update",L"Perbarui"},{L"Later",L"Nanti"},{L"Close",L"Tutup"},{L"Cancel",L"Batal"},{L"Apply",L"Terapkan"},{L"Recommended",L"Disarankan"},{L"Defaults",L"Bawaan"},{L"Refresh",L"Segarkan"},{L"All",L"Semua"},{L"Active",L"Aktif"},{L"User",L"Pengguna"},{L"Ready",L"Siap"}}; return t;
}
Table const& vietnamese() {
    static const Table t{{L"Home",L"Trang chủ"},{L"Debloater",L"Tối ưu hóa"},{L"Performance",L"Hiệu suất"},{L"Privacy & Security",L"Quyền riêng tư & Bảo mật"},{L"Downloads",L"Tải xuống"},{L"Processes",L"Tiến trình"},{L"Latency",L"Độ trễ"},{L"Extras",L"Bổ sung"},{L"Settings",L"Cài đặt"},{L"System",L"Hệ thống"},{L"Processor",L"Bộ xử lý"},{L"Graphics",L"Đồ họa"},{L"Memory",L"Bộ nhớ"},{L"Storage",L"Lưu trữ"},{L"Motherboard",L"Bo mạch chủ"},{L"Display",L"Màn hình"},{L"Uptime",L"Thời gian hoạt động"},{L"Application",L"Ứng dụng"},{L"Language",L"Ngôn ngữ"},{L"Choose the application language.",L"Chọn ngôn ngữ ứng dụng."},{L"These settings are saved automatically.",L"Các cài đặt này được lưu tự động."},{L"Check updates on startup",L"Kiểm tra cập nhật khi khởi động"},{L"Show console",L"Hiện bảng điều khiển"},{L"Start with Windows",L"Khởi động cùng Windows"},{L"System Protection",L"Bảo vệ hệ thống"},{L"System Restore Point",L"Điểm khôi phục hệ thống"},{L"Windows Apps",L"Ứng dụng Windows"},{L"Capabilities",L"Khả năng"},{L"Optional Features",L"Tính năng tùy chọn"},{L"Search",L"Tìm kiếm"},{L"Installed",L"Đã cài đặt"},{L"Not installed",L"Chưa cài đặt"},{L"Install",L"Cài đặt"},{L"Update",L"Cập nhật"},{L"Later",L"Để sau"},{L"Close",L"Đóng"},{L"Cancel",L"Hủy"},{L"Apply",L"Áp dụng"},{L"Recommended",L"Khuyên dùng"},{L"Defaults",L"Mặc định"},{L"Refresh",L"Làm mới"},{L"All",L"Tất cả"},{L"Active",L"Đang hoạt động"},{L"User",L"Người dùng"},{L"Ready",L"Sẵn sàng"}}; return t;
}
Table const& arabic() {
    static const Table t{{L"Home",L"الرئيسية"},{L"Debloater",L"التحسين"},{L"Performance",L"الأداء"},{L"Privacy & Security",L"الخصوصية والأمان"},{L"Downloads",L"التنزيلات"},{L"Processes",L"العمليات"},{L"Latency",L"زمن الاستجابة"},{L"Extras",L"إضافات"},{L"Settings",L"الإعدادات"},{L"System",L"النظام"},{L"Processor",L"المعالج"},{L"Graphics",L"الرسومات"},{L"Memory",L"الذاكرة"},{L"Storage",L"التخزين"},{L"Motherboard",L"اللوحة الأم"},{L"Display",L"الشاشة"},{L"Uptime",L"مدة التشغيل"},{L"Application",L"التطبيق"},{L"Language",L"اللغة"},{L"Choose the application language.",L"اختر لغة التطبيق."},{L"These settings are saved automatically.",L"يتم حفظ هذه الإعدادات تلقائيًا."},{L"Check updates on startup",L"التحقق من التحديثات عند بدء التشغيل"},{L"Show console",L"إظهار وحدة التحكم"},{L"Start with Windows",L"البدء مع Windows"},{L"System Protection",L"حماية النظام"},{L"System Restore Point",L"نقطة استعادة النظام"},{L"Windows Apps",L"تطبيقات Windows"},{L"Capabilities",L"الإمكانات"},{L"Optional Features",L"الميزات الاختيارية"},{L"Search",L"بحث"},{L"Installed",L"مثبّت"},{L"Not installed",L"غير مثبّت"},{L"Install",L"تثبيت"},{L"Update",L"تحديث"},{L"Later",L"لاحقًا"},{L"Close",L"إغلاق"},{L"Cancel",L"إلغاء"},{L"Apply",L"تطبيق"},{L"Recommended",L"موصى به"},{L"Defaults",L"الافتراضيات"},{L"Refresh",L"تحديث"},{L"All",L"الكل"},{L"Active",L"نشط"},{L"User",L"المستخدم"},{L"Ready",L"جاهز"}}; return t;
}
Table const& traditional_chinese() {
    static const Table t{{L"Home",L"首頁"},{L"Debloater",L"精簡最佳化"},{L"Performance",L"效能"},{L"Privacy & Security",L"隱私權與安全性"},{L"Downloads",L"下載"},{L"Processes",L"處理程序"},{L"Latency",L"延遲"},{L"Extras",L"其他"},{L"Settings",L"設定"},{L"System",L"系統"},{L"Processor",L"處理器"},{L"Graphics",L"顯示卡"},{L"Memory",L"記憶體"},{L"Storage",L"儲存空間"},{L"Motherboard",L"主機板"},{L"Display",L"顯示器"},{L"Uptime",L"運作時間"},{L"Application",L"應用程式"},{L"Language",L"語言"},{L"Choose the application language.",L"選擇應用程式語言。"},{L"These settings are saved automatically.",L"這些設定會自動儲存。"},{L"Check updates on startup",L"啟動時檢查更新"},{L"Show console",L"顯示主控台"},{L"Start with Windows",L"隨 Windows 啟動"},{L"System Protection",L"系統保護"},{L"System Restore Point",L"系統還原點"},{L"Windows Apps",L"Windows 應用程式"},{L"Capabilities",L"功能"},{L"Optional Features",L"選用功能"},{L"Search",L"搜尋"},{L"Installed",L"已安裝"},{L"Not installed",L"未安裝"},{L"Install",L"安裝"},{L"Update",L"更新"},{L"Later",L"稍後"},{L"Close",L"關閉"},{L"Cancel",L"取消"},{L"Apply",L"套用"},{L"Recommended",L"建議"},{L"Defaults",L"預設值"},{L"Refresh",L"重新整理"},{L"All",L"全部"},{L"Active",L"作用中"},{L"User",L"使用者"},{L"Ready",L"就緒"}}; return t;
}
Table const& thai() {
    static const Table t{{L"Home",L"หน้าหลัก"},{L"Debloater",L"ปรับแต่ง"},{L"Performance",L"ประสิทธิภาพ"},{L"Privacy & Security",L"ความเป็นส่วนตัวและความปลอดภัย"},{L"Downloads",L"ดาวน์โหลด"},{L"Processes",L"กระบวนการ"},{L"Latency",L"เวลาแฝง"},{L"Extras",L"เพิ่มเติม"},{L"Settings",L"การตั้งค่า"},{L"System",L"ระบบ"},{L"Processor",L"ตัวประมวลผล"},{L"Graphics",L"กราฟิก"},{L"Memory",L"หน่วยความจำ"},{L"Storage",L"พื้นที่เก็บข้อมูล"},{L"Motherboard",L"เมนบอร์ด"},{L"Display",L"จอแสดงผล"},{L"Uptime",L"เวลาทำงาน"},{L"Application",L"แอปพลิเคชัน"},{L"Language",L"ภาษา"},{L"Choose the application language.",L"เลือกภาษาของแอปพลิเคชัน"},{L"These settings are saved automatically.",L"การตั้งค่าเหล่านี้จะถูกบันทึกโดยอัตโนมัติ"},{L"Check updates on startup",L"ตรวจสอบอัปเดตเมื่อเริ่มต้น"},{L"Show console",L"แสดงคอนโซล"},{L"Start with Windows",L"เริ่มพร้อม Windows"},{L"System Protection",L"การป้องกันระบบ"},{L"System Restore Point",L"จุดคืนค่าระบบ"},{L"Windows Apps",L"แอป Windows"},{L"Capabilities",L"ความสามารถ"},{L"Optional Features",L"คุณลักษณะเสริม"},{L"Search",L"ค้นหา"},{L"Installed",L"ติดตั้งแล้ว"},{L"Not installed",L"ยังไม่ได้ติดตั้ง"},{L"Install",L"ติดตั้ง"},{L"Update",L"อัปเดต"},{L"Later",L"ภายหลัง"},{L"Close",L"ปิด"},{L"Cancel",L"ยกเลิก"},{L"Apply",L"นำไปใช้"},{L"Recommended",L"แนะนำ"},{L"Defaults",L"ค่าเริ่มต้น"},{L"Refresh",L"รีเฟรช"},{L"All",L"ทั้งหมด"},{L"Active",L"ใช้งานอยู่"},{L"User",L"ผู้ใช้"},{L"Ready",L"พร้อม"}}; return t;
}

}  // namespace

void set_ui_language(Language language) { g_language.store(language); }
Language ui_language() { return g_language.load(); }

std::wstring loc(std::wstring_view english) {
    const auto language = g_language.load();
    if (language == Language::english) return std::wstring(english);
    auto const& table = language == Language::german ? german()
        : language == Language::spanish ? spanish()
        : language == Language::french ? french()
        : language == Language::russian ? russian()
        : language == Language::simplified_chinese ? simplified_chinese()
        : language == Language::portuguese_brazil ? portuguese_brazil()
        : language == Language::polish ? polish()
        : language == Language::turkish ? turkish()
        : language == Language::japanese ? japanese()
        : language == Language::korean ? korean()
        : language == Language::italian ? italian()
        : language == Language::dutch ? dutch()
        : language == Language::ukrainian ? ukrainian()
        : language == Language::czech ? czech()
        : language == Language::indonesian ? indonesian()
        : language == Language::vietnamese ? vietnamese()
        : language == Language::arabic ? arabic()
        : language == Language::traditional_chinese ? traditional_chinese() : thai();
    if (auto it = table.find(std::wstring(english)); it != table.end()) return it->second;
    return std::wstring(english);
}

}  // namespace winchisel::core
