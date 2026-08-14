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
CloseApplicationsFilter=Capricorn-V*.exe,Capricorn-v*.exe,CapricornCore-V*.exe,CapricornCore-v*.exe
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
Type: files; Name: "{app}\Capricorn-v*.exe"
Type: files; Name: "{app}\core\CapricornCore-V*.exe"
Type: files; Name: "{app}\core\CapricornCore-v*.exe"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Capricorn"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\Capricorn"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch Capricorn"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
var
  DeleteUserData: Boolean;
  DeleteUserDataAsked: Boolean;

procedure DeleteKnownFile(const FileName: String);
begin
  DelTree(FileName, False, True, False);
end;

procedure DeleteSqliteArtifacts(const Root, FileName: String);
begin
  DeleteKnownFile(AddBackslash(Root) + FileName);
  DeleteKnownFile(AddBackslash(Root) + FileName + '-wal');
  DeleteKnownFile(AddBackslash(Root) + FileName + '-shm');
end;

procedure DeleteKnownState(const Root, FileName: String);
begin
  DeleteKnownFile(AddBackslash(Root) + FileName);
  DeleteKnownFile(AddBackslash(Root) + FileName + '.corrupt-*.json');
end;

procedure DeleteManagedAvatars(const Root: String);
begin
  DelTree(AddBackslash(Root) + 'user-avatars', True, True, True);
end;

procedure RemoveKnownEmptyParents(const Root: String);
begin
  RemoveDir(Root);
end;

procedure DeleteLegacyVersionData(const FamilyRoot, DirectoryName: String;
  Version: Integer);
var
  Root: String;
begin
  Root := AddBackslash(FamilyRoot) + DirectoryName;
  DeleteKnownState(Root, 'state-v' + IntToStr(Version) + '.json');
  if (Version >= 73) and (Version <= 96) then
    DeleteSqliteArtifacts(Root, 'chat-v' + IntToStr(Version) + '.sqlite3');
  DeleteManagedAvatars(Root);
  RemoveKnownEmptyParents(Root);
end;

procedure DeleteCurrentAndLegacyUserData;
var
  CurrentRoot: String;
  FamilyRoot: String;
  GenericLegacyRoot: String;
  CacheAppRoot: String;
  Version: Integer;
begin
  FamilyRoot := ExpandConstant('{userappdata}\Capricorn');
  CurrentRoot := AddBackslash(FamilyRoot) + 'Capricorn-v1.0.0';

  DeleteKnownState(CurrentRoot, 'state-v1.0.0.json');
  DeleteKnownState(CurrentRoot, 'state-v129.json');
  for Version := 38 to 128 do
    DeleteKnownState(CurrentRoot, 'state-v' + IntToStr(Version) + '.json');
  DeleteSqliteArtifacts(CurrentRoot, 'chat-v97.sqlite3');
  for Version := 73 to 96 do
    DeleteSqliteArtifacts(CurrentRoot, 'chat-v' + IntToStr(Version) + '.sqlite3');
  DeleteManagedAvatars(CurrentRoot);
  RemoveKnownEmptyParents(CurrentRoot);

  DeleteLegacyVersionData(FamilyRoot, 'Capricorn-V129', 129);
  for Version := 38 to 128 do
  begin
    DeleteLegacyVersionData(FamilyRoot, 'Capricorn-V' + IntToStr(Version), Version);
    DeleteLegacyVersionData(FamilyRoot, 'CapricornV' + IntToStr(Version), Version);
  end;

  GenericLegacyRoot := AddBackslash(FamilyRoot) + 'Capricorn';
  for Version := 38 to 128 do
    DeleteKnownState(GenericLegacyRoot, 'state-v' + IntToStr(Version) + '.json');
  for Version := 73 to 96 do
    DeleteSqliteArtifacts(GenericLegacyRoot, 'chat-v' + IntToStr(Version) + '.sqlite3');
  DeleteManagedAvatars(GenericLegacyRoot);
  RemoveKnownEmptyParents(GenericLegacyRoot);
  RemoveKnownEmptyParents(FamilyRoot);

  CacheAppRoot := ExpandConstant('{localappdata}\Capricorn\Capricorn-v1.0.0');
  DelTree(AddBackslash(CacheAppRoot) + 'cache\avatars-v97', True, True, True);
  RemoveDir(AddBackslash(CacheAppRoot) + 'cache');
  RemoveKnownEmptyParents(CacheAppRoot);
  RemoveDir(ExpandConstant('{localappdata}\Capricorn'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Answer: Integer;
begin
  if (CurUninstallStep = usUninstall) and (not DeleteUserDataAsked) then
  begin
    DeleteUserDataAsked := True;
    if not UninstallSilent then
    begin
      Answer := MsgBox(
        '是否同时删除当前 Windows 用户的 Capricorn 用户配置？' + #13#10 + #13#10 +
        '选择“是”将永久删除本产品的配置、聊天记录、记忆状态、用户头像、缓存、损坏备份，以及迁移代码识别的旧版本数据。此操作不可恢复。' + #13#10 + #13#10 +
        '用户自行导出到其他位置的人格包不会被删除。',
        mbConfirmation, MB_YESNO or MB_DEFBUTTON2);
      DeleteUserData := Answer = IDYES;
    end;
  end;

  if (CurUninstallStep = usPostUninstall) and DeleteUserData then
    DeleteCurrentAndLegacyUserData;
end;
