; 完整离线安装器；载荷只能来自普通客户端构建目录。
#ifndef SourceDir
  #error SourceDir must point to the normal client setup payload directory
#endif
#ifndef AppVersion
  #error AppVersion must contain the QmClient version
#endif
#ifndef OutputDir
  #error OutputDir must contain the output directory
#endif

#ifndef SetupAppId
  #define SetupAppId "{{2FA2A2F9-6225-480D-9B99-250032246A78}"
#endif

[Setup]
AppId={#SetupAppId}
AppName=QmClient
AppVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\QmClient
DefaultGroupName=QmClient
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=QmClient-Setup
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern
UninstallDisplayIcon={app}\DDNet.exe
CloseApplications=yes
RestartApplications=no
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
UsePreviousTasks=yes
UninstallLogMode=append

[Languages]
Name: "zh_cn"; MessagesFile: "QmClient-zh_CN.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[InstallDelete]
; 只清理已知旧发布文件，不递归删除安装目录或用户资源。
Type: files; Name: "{app}\data\fonts\Phosphor\Phosphor-Duotone.ttf"
Type: files; Name: "{app}\data\qmclient\icons\qm_icons_duotone_msdf.json"
Type: files; Name: "{app}\data\qmclient\icons\qm_icons_duotone_msdf.png"
Type: files; Name: "{app}\data\fonts\NotoSansBalinese-Regular.ttf"
Type: files; Name: "{app}\data\fonts\NotoSansBamum-Regular.ttf"
Type: files; Name: "{app}\data\fonts\NotoSansBatak-Regular.ttf"
Type: files; Name: "{app}\data\fonts\NotoSansEgyptianHieroglyphs-Regular.ttf"
Type: files; Name: "{app}\data\fonts\NotoSansJavanese-Regular.ttf"
Type: files; Name: "{app}\data\fonts\NotoSansOriya-Regular.ttf"
Type: files; Name: "{app}\data\fonts\NotoSansSC-VF.ttf"
Type: files; Name: "{app}\data\fonts\NotoSansTaiLe-Regular.ttf"
Type: files; Name: "{app}\data\fonts\NotoSansThai-Regular.ttf"
Type: files; Name: "{app}\data\fonts\NotoSansVai-Regular.ttf"
Type: files; Name: "{app}\data\fonts\NotoSansYi-Regular.ttf"
Type: files; Name: "{app}\data\fonts\NotoSerifTibetan-Regular.ttf"
Type: files; Name: "{app}\data\fonts\方正屏显雅宋.TTF"
Type: files; Name: "{app}\data\fonts\霞鹜新晰黑.ttf"
Type: files; Name: "{app}\data\fonts\霞鹜新致宋.ttf"
Type: files; Name: "{app}\data\fonts\Maple Mono Normal\MapleMonoNormal-CN-Bold.ttf"
Type: files; Name: "{app}\data\fonts\Maple Mono Normal\MapleMonoNormal-CN-Light.ttf"
Type: files; Name: "{app}\data\fonts\Maple Mono Normal\MapleMonoNormal-CN-Regular.ttf"
Type: files; Name: "{app}\data\fonts\Maple Mono Normal\LICENSE.txt"
Type: files; Name: "{app}\data\fonts\Maple Mono Normal\config.json"
Type: files; Name: "{app}\data\fonts\Ping Fang SC\PingFangSC-Light.ttf"
Type: files; Name: "{app}\data\fonts\Ping Fang SC\PingFangSC-Medium.ttf"
Type: files; Name: "{app}\data\fonts\Ping Fang SC\PingFangSC-Regular.ttf"
Type: files; Name: "{app}\data\fonts\Ping Fang SC\index.css"
Type: files; Name: "{app}\data\fonts\霞鹜文楷\LXGWWenKai-Light.ttf"
Type: files; Name: "{app}\data\fonts\霞鹜文楷\LXGWWenKai-Medium.ttf"
Type: files; Name: "{app}\data\fonts\霞鹜文楷\LXGWWenKai-Regular.ttf"
Type: files; Name: "{app}\data\fonts\霞鹜文楷\OFL.txt"
Type: files; Name: "{app}\data\qmclient\fonts\方正屏显雅宋.TTF"
Type: files; Name: "{app}\data\qmclient\fonts\霞鹜新晰黑.ttf"
Type: files; Name: "{app}\data\qmclient\fonts\霞鹜新致宋.ttf"
Type: files; Name: "{app}\data\qmclient\fonts\Maple Mono Normal\MapleMonoNormal-CN-Bold.ttf"
Type: files; Name: "{app}\data\qmclient\fonts\Maple Mono Normal\MapleMonoNormal-CN-Light.ttf"
Type: files; Name: "{app}\data\qmclient\fonts\Maple Mono Normal\MapleMonoNormal-CN-Regular.ttf"
Type: files; Name: "{app}\data\qmclient\fonts\Maple Mono Normal\LICENSE.txt"
Type: files; Name: "{app}\data\qmclient\fonts\Maple Mono Normal\config.json"
Type: files; Name: "{app}\data\qmclient\fonts\Ping Fang SC\PingFangSC-Light.ttf"
Type: files; Name: "{app}\data\qmclient\fonts\Ping Fang SC\PingFangSC-Medium.ttf"
Type: files; Name: "{app}\data\qmclient\fonts\Ping Fang SC\PingFangSC-Regular.ttf"
Type: files; Name: "{app}\data\qmclient\fonts\Ping Fang SC\index.css"
Type: files; Name: "{app}\data\qmclient\fonts\霞鹜文楷\LXGWWenKai-Light.ttf"
Type: files; Name: "{app}\data\qmclient\fonts\霞鹜文楷\LXGWWenKai-Medium.ttf"
Type: files; Name: "{app}\data\qmclient\fonts\霞鹜文楷\LXGWWenKai-Regular.ttf"
Type: files; Name: "{app}\data\qmclient\fonts\霞鹜文楷\OFL.txt"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[INI]
Filename: "{app}\QmClient-Setup.ini"; Section: "Setup"; Key: "Version"; String: "{#AppVersion}"; Flags: uninsdeleteentry uninsdeletesectionifempty

[UninstallDelete]
Type: files; Name: "{app}\QmClient-Setup.ini"

[Icons]
Name: "{group}\QmClient"; Filename: "{app}\DDNet.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\QmClient"; Filename: "{app}\DDNet.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\DDNet.exe"; Description: "Launch QmClient"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
