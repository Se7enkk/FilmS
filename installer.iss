[Setup]
AppName=FilmS
AppVerName=FilmS 1.2
AppPublisher=FilmS
DefaultDirName={autopf}\FilmS
DefaultGroupName=FilmS
DisableProgramGroupPage=yes
OutputDir=C:\Users\Administrator\Doubao\chats\2026-10-27\new-chat\installer
OutputBaseFilename=FilmS-Setup-1.2
SetupIconFile=C:\Users\Administrator\Doubao\chats\2026-10-27\new-chat\build\qt\app\SpektraLab-W.ico
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加选项:"; Flags: checkedonce

[Files]
Source: "C:\Users\Administrator\Doubao\chats\2026-10-27\new-chat\build\qt\app\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\FilmS"; Filename: "{app}\SpektraLabQt.exe"; IconFilename: "{app}\SpektraLab-W.ico"
Name: "{group}\卸载 FilmS"; Filename: "{uninstallexe}"
Name: "{commondesktop}\FilmS"; Filename: "{app}\SpektraLabQt.exe"; IconFilename: "{app}\SpektraLab-W.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\SpektraLabQt.exe"; Description: "启动 FilmS"; Flags: nowait postinstall skipifsilent
