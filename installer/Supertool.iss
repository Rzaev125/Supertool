#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef PayloadDir
  #error PayloadDir is required; use scripts/build-installer.ps1
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
#ifndef OutputName
  #define OutputName "Supertool-" + AppVersion + "-win-x64-setup"
#endif

[Setup]
AppId={{00BEDB6F-6D79-4F1C-AAB6-B13F51553090}
AppName=Supertool
AppVersion={#AppVersion}
AppVerName=Supertool {#AppVersion}
AppPublisher=Supertool
VersionInfoVersion={#AppVersion}.0
SetupIconFile=..\resources\branding\supertool.ico
#ifndef IsolatedRoot
DefaultDirName={code:DefaultDirectory}
#else
DefaultDirName={#IsolatedRoot}\app
#endif
DisableDirPage=yes
DisableProgramGroupPage=yes
UsePreviousAppDir=no
#ifndef IsolatedRoot
UsePreviousTasks=yes
#else
UsePreviousTasks=no
#endif
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
AppMutex=Local\ShiftWheel.Tray
SetupMutex=Local\ShiftWheel.Setup
CloseApplications=no
RestartApplications=no
AllowCancelDuringInstall=yes
UninstallDisplayIcon={app}\Supertool.exe
UninstallDisplayName=Supertool
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2/fast
SolidCompression=yes
LZMANumBlockThreads=2
WizardStyle=modern
SetupLogging=yes
DisableWelcomePage=no
DisableReadyPage=no
AlwaysRestart=no
#ifdef IsolatedRoot
CreateUninstallRegKey=no
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

#ifndef IsolatedRoot
[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Icons]
Name: "{userprograms}\Supertool"; Filename: "{app}\Supertool.exe"; Parameters: "--panel"; WorkingDir: "{app}"
Name: "{userdesktop}\Supertool"; Filename: "{app}\Supertool.exe"; Parameters: "--panel"; WorkingDir: "{app}"; Tasks: desktopicon
#endif

[Files]
; Payload is an immutable filtered stage; settings are never in [Files].
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Existing shortcuts and the legacy update helper must open the new product.
Source: "{#PayloadDir}\Supertool.exe"; DestDir: "{app}"; DestName: "ShiftWheel.exe"; Flags: ignoreversion; Check: LegacyInstallation

; No [Run], [InstallDelete], or [UninstallDelete]: never kill applications,
; start background work, or recursively delete the user's application folder.
[Code]
function GetFileAttributes(FileName: String): LongWord;
  external 'GetFileAttributesW@kernel32.dll stdcall';

function LegacyInstallation: Boolean;
#ifndef IsolatedRoot
var
  Location: String;
#endif
begin
  Result := False;
#ifndef IsolatedRoot
  if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{00BEDB6F-6D79-4F1C-AAB6-B13F51553090}_is1', 'InstallLocation', Location) then
    Result := CompareText(RemoveBackslashUnlessRoot(Location), ExpandConstant('{localappdata}\Programs\ShiftWheel')) = 0;
#endif
end;

function ExpectedDirectory: String;
begin
#ifdef IsolatedRoot
  Result := ExpandFileName('{#IsolatedRoot}\app');
#else
  if LegacyInstallation then Result := ExpandConstant('{localappdata}\Programs\ShiftWheel')
  else Result := ExpandConstant('{localappdata}\Programs\Supertool');
#endif
end;

function DefaultDirectory(Param: String): String;
begin
  Result := ExpectedDirectory;
end;

function HasReparseParent(Path: String): Boolean;
var
  Parent: String;
  Attributes: LongWord;
begin
  Result := False;
  repeat
    Attributes := GetFileAttributes(Path);
    if (Attributes <> $FFFFFFFF) and ((Attributes and $400) <> 0) then begin
      Result := True;
      Exit;
    end;
    Parent := ExtractFileDir(Path);
    if Parent = Path then Exit;
    Path := Parent;
  until Path = '';
end;

function HasReparseChild(Path: String): Boolean;
var
  Item: TFindRec;
begin
  Result := False;
  if FindFirst(AddBackslash(Path) + '*', Item) then begin
    try
      repeat
        if (Item.Name <> '.') and (Item.Name <> '..') then begin
          if (Item.Attributes and $400) <> 0 then begin
            Result := True;
            Exit;
          end;
          if (Item.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
            if HasReparseChild(AddBackslash(Path) + Item.Name) then begin
              Result := True;
              Exit;
            end;
        end;
      until not FindNext(Item);
    finally
      FindClose(Item);
    end;
  end;
end;

function DirectoryError(Path: String): String;
begin
  Result := '';
  if CompareText(RemoveBackslashUnlessRoot(ExpandFileName(Path)), RemoveBackslashUnlessRoot(ExpectedDirectory)) <> 0 then
    Result := 'Supertool installs only into its dedicated per-user application folder.'
  else if HasReparseParent(Path) or HasReparseChild(Path) then
    Result := 'The application folder contains a directory link. Choose a regular folder before installing or uninstalling Supertool.';
end;

function InitializeSetup: Boolean;
var
  Error: String;
begin
  Result := False;
#ifdef IsolatedRoot
  if ExpandConstant('{param:SHIFTWHEELTEST|0}') <> '1' then begin
    SuppressibleMsgBox('This isolated test build requires /SHIFTWHEELTEST=1.', mbError, MB_OK, IDOK);
    Exit;
  end;
#endif
  Error := DirectoryError(ExpandConstant('{param:DIR|' + ExpectedDirectory + '}'));
  if Error <> '' then begin
    SuppressibleMsgBox(Error, mbError, MB_OK, IDOK);
    Exit;
  end;
  Result := True;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := DirectoryError(ExpandConstant('{app}'));
  if (Result = '') and CheckForMutexes('Local\ShiftWheel.Tray') then
    Result := 'Supertool is running. Finish or cancel the current conversion, then choose Exit from its tray menu and retry.';
end;

function InitializeUninstall: Boolean;
var
  Error: String;
begin
  Error := DirectoryError(ExpandConstant('{app}'));
  Result := Error = '';
  if not Result then SuppressibleMsgBox(Error, mbError, MB_OK, IDOK);
end;


procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
#ifndef IsolatedRoot
var
  Command: String;
#endif
begin
#ifndef IsolatedRoot
  if CurUninstallStep = usUninstall then
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Supertool', Command) then
      if CompareText(Command, '"' + ExpandConstant('{app}\Supertool.exe') + '" --startup') = 0 then
        RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Supertool');
#endif
end;
