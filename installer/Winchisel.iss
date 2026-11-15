#ifndef StagingDir
  #error StagingDir must be supplied by the release build.
#endif
#ifndef OutputDir
  #error OutputDir must be supplied by the release build.
#endif
#ifndef ReleaseVersion
  #error ReleaseVersion must be supplied by the release build.
#endif
#ifndef VersionInfoVersion
  #error VersionInfoVersion must be supplied by the release build.
#endif

[Setup]
AppId={{A1C0E001-0A00-4000-8000-000000000001}
AppName=Winchisel
AppVersion={#ReleaseVersion}
VersionInfoVersion={#VersionInfoVersion}
AppPublisher=lejy
DefaultDirName={autopf}\Winchisel
DefaultGroupName=Winchisel
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=Winchisel-Setup-x64
WizardStyle=modern
SetupIconFile={#SourcePath}\..\assets\logo.ico
Compression=lzma2/ultra
SolidCompression=yes
LZMAUseSeparateProcess=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22621
PrivilegesRequired=admin
UninstallDisplayIcon={app}\Winchisel.exe
CloseApplications=yes
RestartApplications=yes
SetupLogging=yes

[Files]
Source: "{#StagingDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Winchisel"; Filename: "{app}\Winchisel.exe"
Name: "{autodesktop}\Winchisel"; Filename: "{app}\Winchisel.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"

[Run]
Filename: "{app}\Winchisel.exe"; Description: "Launch Winchisel"; Flags: nowait postinstall skipifsilent

[Code]
procedure CurPageChanged(CurPageID: Integer);
begin
  { Keep a previously created desktop icon across updates and reinstalls:
    Inno removes task-bound icons when the task is not selected in the
    current run, and the task defaults to unchecked. Pre-check it when the
    icon already exists so only an explicit uncheck removes it. A prior
    /TASKS= choice is preserved via the or-combination. }
  if (CurPageID = wpSelectTasks) and (WizardForm.TasksList.Items.Count = 1) then
    WizardForm.TasksList.Checked[0] :=
      WizardForm.TasksList.Checked[0] or FileExists(ExpandConstant('{autodesktop}\Winchisel.lnk'));
end;
