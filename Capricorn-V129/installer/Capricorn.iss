#define ProjectName GetEnv("CAPRICORN_INSTALL_PROJECT")
#define AppVersion GetEnv("CAPRICORN_INSTALL_VERSION")
#define SourceDir GetEnv("CAPRICORN_INSTALL_SOURCE")
#define OutputDir GetEnv("CAPRICORN_INSTALL_OUTPUT")
#define SetupIcon GetEnv("CAPRICORN_INSTALL_ICON")
#define AppExeName GetEnv("CAPRICORN_INSTALL_EXE")

[Setup]
AppId={{AE4A86C1-203C-412C-966F-7DA7811B5C09}
AppName=Capricorn
AppVersion={#AppVersion}
AppVerName=Capricorn {#AppVersion}
AppPublisher=Capricorn
DefaultDirName={autopf}\Capricorn
DefaultGroupName=Capricorn
DisableProgramGroupPage=yes
DisableDirPage=no
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
SetupArchitecture=x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename={#ProjectName}-Setup-x64
SetupIconFile={#SetupIcon}
UninstallDisplayName=Capricorn
UninstallDisplayIcon={app}\{#AppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
CloseApplicationsFilter=Capricorn-V*.exe,CapricornCore-V*.exe
RestartApplications=no
UsePreviousAppDir=yes
SetupMutex=Capricorn-Setup-Mutex
VersionInfoCompany=Capricorn
VersionInfoDescription=Capricorn Desktop Pet Installer
VersionInfoProductName=Capricorn
VersionInfoProductVersion={#AppVersion}
VersionInfoVersion={#AppVersion}

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[InstallDelete]
Type: files; Name: "{app}\Capricorn-V*.exe"
Type: files; Name: "{app}\core\CapricornCore-V*.exe"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Capricorn"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\Capricorn"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[UninstallDelete]
Type: filesandordirs; Name: "{userappdata}\Capricorn"
Type: filesandordirs; Name: "{localappdata}\Capricorn"
Type: filesandordirs; Name: "{app}\*"

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch Capricorn"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
