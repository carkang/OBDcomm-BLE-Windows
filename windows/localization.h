#pragma once

#include <windows.h>
#include <cstddef>

enum class Text {
    UpdateScanError,
    ScanError,
    AdapterTimeout,
    CheckCanceled,
    DeviceUnavailable,
    BluetoothOff,
    BluetoothBusy,
    WriteFailed,
    Cancel,
    CancelConnection,
    CheckingAdapter,
    ClearRx,
    ClearInput,
    UuidMissing,
    ConfirmPassword,
    Connect,
    Connected,
    Connecting,
    ConnectionTimeout,
    CopyReceived,
    CopyToken,
    ClipboardError,
    FileLimit,
    SaveError,
    PasswordError,
    StartScanError,
    ScanTimerError,
    DeviceDisconnected,
    Disconnect,
    Disconnected,
    Encoding,
    PasswordMismatch,
    EnterPassword,
    InvalidUuid,
    Exit,
    FileReadError,
    IncorrectPassword,
    LoadFile,
    NewPassword,
    TokenMissing,
    NoNotifications,
    ReceiveUuid,
    ReceiveSound,
    ReceivedData,
    RequirePassword,
    RestoreDefaults,
    Save,
    SaveDisconnects,
    ScanFailed,
    ScanFinished,
    ScanStoppedStatus,
    ScanStopped,
    Scanning,
    ScanningCount,
    Send,
    SendUuid,
    SendData,
    SendLineEnding,
    ConfirmRawFile,
    ServiceDiscoveryError,
    Settings,
    ShowCopyToken,
    ScanStartTimeout,
    StopScan,
    SubscriptionError,
    TxNotWritable,
    ScanUnsupported,
    CentralUnsupported,
    Unlock,
    UnlockTitle,
    UnnamedDevice,
    NoAdapter,
    BluetoothRestricted,
    WriteTimeout,
    Count
};

namespace localization {
enum class Language { English, Chinese, Spanish, Japanese };

constexpr Language languageFor(LANGID id) {
    switch (PRIMARYLANGID(id)) {
    case LANG_CHINESE: return Language::Chinese;
    case LANG_SPANISH: return Language::Spanish;
    case LANG_JAPANESE: return Language::Japanese;
    default: return Language::English;
    }
}

inline Language systemLanguage() {
    static const auto language = languageFor(GetUserDefaultUILanguage());
    return language;
}

struct Translation {
    const wchar_t* english;
    const wchar_t* chinese;
    const wchar_t* spanish;
    const wchar_t* japanese;
};

inline constexpr Translation translations[] = {
    {L"An unexpected error occurred updating scan results.", L"更新扫描结果时发生程序错误。", L"Error inesperado al actualizar la búsqueda.", L"スキャン結果の更新中に予期しないエラーが発生しました。"}, // UpdateScanError
    {L"An unexpected error occurred while scanning.", L"扫描遇到程序错误。", L"Error inesperado durante la búsqueda.", L"スキャン中に予期しないエラーが発生しました。"}, // ScanError
    {L"Bluetooth adapter check timed out. Check the Bluetooth driver.", L"蓝牙适配器检测超时，请检查蓝牙驱动。", L"Se agotó el tiempo de espera. Compruebe el controlador Bluetooth.", L"Bluetooth アダプターの確認がタイムアウトしました。ドライバーを確認してください。"}, // AdapterTimeout
    {L"Bluetooth check canceled", L"已取消蓝牙检测", L"Comprobación de Bluetooth cancelada", L"Bluetooth の確認をキャンセルしました"}, // CheckCanceled
    {L"Bluetooth device unavailable", L"找不到蓝牙设备", L"Dispositivo Bluetooth no disponible", L"Bluetooth デバイスが見つかりません"}, // DeviceUnavailable
    {L"Bluetooth is off or unavailable. Check Windows Bluetooth settings.", L"蓝牙已关闭或不可用，请检查 Windows 蓝牙设置。", L"Bluetooth está apagado o no está disponible. Revise la configuración de Windows.", L"Bluetooth がオフか利用できません。Windows の設定を確認してください。"}, // BluetoothOff
    {L"Bluetooth resources are busy. Try again shortly.", L"蓝牙资源正被占用，请稍后重试。", L"Bluetooth está ocupado. Inténtelo de nuevo en breve.", L"Bluetooth は使用中です。しばらくしてから再試行してください。"}, // BluetoothBusy
    {L"Bluetooth write failed", L"蓝牙写入失败", L"Error al escribir por Bluetooth", L"Bluetooth への書き込みに失敗しました"}, // WriteFailed
    {L"Cancel", L"取消", L"Cancelar", L"キャンセル"}, // Cancel
    {L"Cancel connection", L"取消连接", L"Cancelar conexión", L"接続を中止"}, // CancelConnection
    {L"Checking the Windows Bluetooth adapter…", L"正在检测 Windows 蓝牙适配器…", L"Comprobando el adaptador Bluetooth de Windows…", L"Windows の Bluetooth アダプターを確認中…"}, // CheckingAdapter
    {L"Clear RX", L"清除接收", L"Borrar RX", L"受信を消去"}, // ClearRx
    {L"Clear input", L"清除输入", L"Borrar entrada", L"入力を消去"}, // ClearInput
    {L"Configured UUID not found", L"找不到配置的 UUID", L"No se encontró el UUID configurado", L"設定した UUID が見つかりません"}, // UuidMissing
    {L"Confirm password", L"确认新密码", L"Confirmar contraseña", L"パスワードの確認"}, // ConfirmPassword
    {L"Connect", L"连接", L"Conectar", L"接続"}, // Connect
    {L"Connected · ", L"已连接 · ", L"Conectado · ", L"接続済み · "}, // Connected
    {L"Connecting · ", L"正在连接 · ", L"Conectando · ", L"接続中 · "}, // Connecting
    {L"Connection timed out", L"连接超时", L"Se agotó el tiempo de conexión", L"接続がタイムアウトしました"}, // ConnectionTimeout
    {L"Copy received", L"复制接收数据", L"Copiar datos RX", L"受信データをコピー"}, // CopyReceived
    {L"Copy token", L"复制 token", L"Copiar token", L"トークンをコピー"}, // CopyToken
    {L"Could not copy to clipboard", L"无法复制到剪贴板", L"No se pudo copiar al portapapeles", L"クリップボードにコピーできませんでした"}, // ClipboardError
    {L"Could not read the file or it exceeds 8 MiB.", L"无法读取文件，或文件超过 8 MiB。", L"No se pudo leer el archivo o supera los 8 MiB.", L"ファイルを読み込めないか、8 MiB を超えています。"}, // FileLimit
    {L"Could not save settings.", L"无法保存设置。", L"No se pudo guardar la configuración.", L"設定を保存できませんでした。"}, // SaveError
    {L"Could not set the password.", L"无法设置密码。", L"No se pudo establecer la contraseña.", L"パスワードを設定できませんでした。"}, // PasswordError
    {L"Could not start scanning.", L"无法启动扫描。", L"No se pudo iniciar la búsqueda.", L"スキャンを開始できませんでした。"}, // StartScanError
    {L"Could not start the Bluetooth scan timer.", L"无法启动蓝牙扫描计时器。", L"No se pudo iniciar el temporizador de búsqueda Bluetooth.", L"Bluetooth スキャンのタイマーを開始できませんでした。"}, // ScanTimerError
    {L"Device disconnected", L"设备已断开", L"Dispositivo desconectado", L"デバイスが切断されました"}, // DeviceDisconnected
    {L"Disconnect", L"断开", L"Desconectar", L"切断"}, // Disconnect
    {L"Disconnected", L"未连接", L"Sin conexión", L"未接続"}, // Disconnected
    {L"Encoding", L"编码", L"Codificación", L"文字コード"}, // Encoding
    {L"Enter a password and confirm it.", L"请输入密码，并确认两次密码一致。", L"Introduzca y confirme la contraseña.", L"パスワードを入力し、確認してください。"}, // PasswordMismatch
    {L"Enter password", L"输入密码", L"Introducir contraseña", L"パスワードを入力"}, // EnterPassword
    {L"Enter valid full UUIDs.", L"请输入有效的完整 UUID。", L"Introduzca UUID completos y válidos.", L"有効な UUID を完全な形式で入力してください。"}, // InvalidUuid
    {L"Exit", L"退出", L"Salir", L"終了"}, // Exit
    {L"File read failed", L"文件读取失败", L"Error al leer el archivo", L"ファイルの読み込みに失敗しました"}, // FileReadError
    {L"Incorrect password.", L"密码错误。", L"Contraseña incorrecta.", L"パスワードが正しくありません。"}, // IncorrectPassword
    {L"Load file", L"载入发送文件", L"Cargar archivo", L"ファイルを読み込む"}, // LoadFile
    {L"New password", L"输入新密码", L"Nueva contraseña", L"新しいパスワード"}, // NewPassword
    {L"No token with a matching length found", L"未找到长度匹配的 token", L"No se encontró un token de la longitud indicada", L"長さが一致するトークンが見つかりません"}, // TokenMissing
    {L"RX notifications unavailable", L"接收特征不支持通知", L"Las notificaciones RX no están disponibles", L"受信特性の通知を利用できません"}, // NoNotifications
    {L"Receive UUID", L"接收 UUID", L"UUID de recepción", L"受信 UUID"}, // ReceiveUuid
    {L"Receive sound (at least 1.5 seconds apart)", L"接收提示音（间隔至少 1.5 秒）", L"Sonido de recepción (intervalo mínimo de 1,5 s)", L"受信音（1.5 秒以上の間隔）"}, // ReceiveSound
    {L"Received data", L"接收数据", L"Datos recibidos", L"受信データ"}, // ReceivedData
    {L"Require a password at startup", L"使用密码（每次启动时验证）", L"Pedir contraseña al iniciar", L"起動時にパスワードを要求"}, // RequirePassword
    {L"Restore defaults", L"恢复默认值", L"Restablecer", L"既定値に戻す"}, // RestoreDefaults
    {L"Save", L"保存", L"Guardar", L"保存"}, // Save
    {L"Saving settings disconnects the current device.", L"保存设置会断开当前连接。", L"Al guardar se desconectará el dispositivo actual.", L"設定を保存すると現在のデバイスが切断されます。"}, // SaveDisconnects
    {L"Scan failed · ", L"扫描失败 · ", L"Error de búsqueda · ", L"スキャン失敗 · "}, // ScanFailed
    {L"Scan finished · double-click to connect", L"扫描结束 · 双击设备连接", L"Búsqueda finalizada · Doble clic para conectar", L"スキャン完了 · ダブルクリックで接続"}, // ScanFinished
    {L"Scan stopped · Bluetooth status ", L"扫描已停止 · 蓝牙状态 ", L"Búsqueda detenida · Estado de Bluetooth: ", L"スキャン停止 · Bluetooth の状態: "}, // ScanStoppedStatus
    {L"Scan stopped · double-click to connect", L"扫描已停止 · 双击设备连接", L"Búsqueda detenida · Doble clic para conectar", L"スキャン停止 · ダブルクリックで接続"}, // ScanStopped
    {L"Scanning · double-click to connect", L"扫描中 · 双击设备连接", L"Buscando · Doble clic para conectar", L"スキャン中 · ダブルクリックで接続"}, // Scanning
    {L"Scanning · double-click to connect · Found ", L"扫描中 · 双击设备连接 · 已发现 ", L"Buscando · Doble clic para conectar · Encontrados: ", L"スキャン中 · ダブルクリックで接続 · 検出数: "}, // ScanningCount
    {L"Send", L"发送", L"Enviar", L"送信"}, // Send
    {L"Send UUID", L"发送 UUID", L"UUID de envío", L"送信 UUID"}, // SendUuid
    {L"Send data", L"发送内容", L"Datos para enviar", L"送信データ"}, // SendData
    {L"Send line ending", L"发送换行", L"Fin de línea de envío", L"送信時の改行"}, // SendLineEnding
    {L"Send this file as raw bytes without a newline?", L"原样发送文件，不附加换行。确认发送？", L"¿Enviar este archivo tal cual, sin añadir un salto de línea?", L"改行を追加せず、このファイルをそのまま送信しますか？"}, // ConfirmRawFile
    {L"Service discovery failed", L"发现服务失败", L"Error al buscar servicios", L"サービスの検出に失敗しました"}, // ServiceDiscoveryError
    {L"Settings", L"设置", L"Configuración", L"設定"}, // Settings
    {L"Show Copy token button", L"显示复制 token 按钮", L"Mostrar el botón Copiar token", L"トークンのコピーボタンを表示"}, // ShowCopyToken
    {L"Starting the BLE scanner timed out.", L"启动 BLE 扫描超时。", L"Se agotó el tiempo de inicio de la búsqueda BLE.", L"BLE スキャンの開始がタイムアウトしました。"}, // ScanStartTimeout
    {L"Stop scan", L"停止扫描", L"Detener búsqueda", L"スキャン停止"}, // StopScan
    {L"Subscription failed", L"订阅失败", L"Error de suscripción", L"通知の購読に失敗しました"}, // SubscriptionError
    {L"TX characteristic is not writable", L"发送特征不可写", L"No se puede escribir en la característica TX", L"送信特性に書き込めません"}, // TxNotWritable
    {L"The Bluetooth adapter or driver does not support BLE scanning.", L"当前蓝牙适配器或驱动不支持 BLE 扫描。", L"El adaptador o controlador Bluetooth no admite la búsqueda BLE.", L"Bluetooth アダプターまたはドライバーが BLE スキャンに対応していません。"}, // ScanUnsupported
    {L"The Bluetooth adapter or driver does not support the BLE central role.", L"当前蓝牙适配器或驱动不支持 BLE 中央设备模式。", L"El adaptador o controlador Bluetooth no admite el rol central BLE.", L"Bluetooth アダプターまたはドライバーが BLE のセントラルロールに対応していません。"}, // CentralUnsupported
    {L"Unlock", L"解锁", L"Desbloquear", L"ロック解除"}, // Unlock
    {L"Unlock OBDcomm", L"解锁 OBDcomm", L"Desbloquear OBDcomm", L"OBDcomm のロック解除"}, // UnlockTitle
    {L"Unnamed device", L"未广播名称", L"Dispositivo sin nombre", L"名前のないデバイス"}, // UnnamedDevice
    {L"Windows found no available Bluetooth adapter. Check Bluetooth settings and the driver.", L"Windows 未找到可用的蓝牙适配器，请检查蓝牙开关和驱动。", L"Windows no encontró un adaptador Bluetooth disponible. Compruebe la configuración y el controlador.", L"利用可能な Bluetooth アダプターが見つかりません。Windows の設定とドライバーを確認してください。"}, // NoAdapter
    {L"Windows restricted Bluetooth access. Check permissions or system policy.", L"Windows 限制了蓝牙访问，请检查系统权限或管理策略。", L"Windows restringió el acceso a Bluetooth. Revise los permisos o la directiva del sistema.", L"Windows が Bluetooth へのアクセスを制限しました。権限またはシステムポリシーを確認してください。"}, // BluetoothRestricted
    {L"Write timed out. Disconnected.", L"写入超时，已断开", L"Se agotó el tiempo de escritura. Dispositivo desconectado.", L"書き込みがタイムアウトし、デバイスが切断されました。"}, // WriteTimeout
};
static_assert(sizeof(translations) / sizeof(translations[0]) == static_cast<std::size_t>(Text::Count));
}

inline const wchar_t* tr(Text text) {
    const auto& translation = localization::translations[static_cast<std::size_t>(text)];
    switch (localization::systemLanguage()) {
    case localization::Language::Chinese: return translation.chinese;
    case localization::Language::Spanish: return translation.spanish;
    case localization::Language::Japanese: return translation.japanese;
    default: return translation.english;
    }
}

inline const wchar_t* uiFont() {
    switch (localization::systemLanguage()) {
    case localization::Language::Chinese: return L"Microsoft YaHei UI";
    case localization::Language::Japanese: return L"Yu Gothic UI";
    default: return L"Segoe UI";
    }
}
